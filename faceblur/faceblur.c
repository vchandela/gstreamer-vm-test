#include <gst/gst.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>

#define SEGMENT_DURATION 15000 // Duration for each segment in milliseconds

typedef struct _CustomData
{
    GstElement *pipeline;
    GstElement *source;

    GstElement *video_queue;
    GstElement *video_convert;
    GstElement *face_blur;
    GstElement *video_convert2;
    GstElement *x264_enc;
    GstElement *video_tee;
    GstElement *video_flv_queue;

    GstElement *audio_source;
    GstElement *audio_queue;
    GstElement *audio_convert;
    GstElement *audio_resample;
    GstElement *avenc_aac;
    GstElement *audio_tee;
    GstElement *audio_flv_queue;

    GstElement *flv_mux;
    GstElement *flv_filesink;
    GstElement *split_mux_sink;
    GstElement *gcs_sink;
} CustomData;

// Function to get the current time in milliseconds since the Unix epoch
static long long current_time_millis()
{
    struct timeval time_now;
    gettimeofday(&time_now, NULL);
    return (long long)(time_now.tv_sec) * 1000 + (long long)(time_now.tv_usec) / 1000;
}

static gchar *format_location_callback(GstElement *splitmuxsink, guint fragment_id, gpointer user_data)
{
    // Get the (Unix epoch time) for start and end time
    long long start_time = current_time_millis();
    long long end_time = start_time + SEGMENT_DURATION;

    gchar *filename = g_strdup_printf("vm/4/%lld_%lld.mp4", start_time, end_time);

    GstElement *gcs_sink = GST_ELEMENT(user_data); // Retrieve gcs_sink passed via user_data
    if (gcs_sink)
    {
        g_object_set(gcs_sink, "key", filename, NULL); // Set the key dynamically
    }

    return filename;
}

static gboolean link_elements_with_video_filter(GstElement *element1, GstElement *element2)
{
    gboolean link_ok;
    GstCaps *caps;

    caps = gst_caps_new_simple("video/x-raw",
                               "format", G_TYPE_STRING, "I420",
                               "width", G_TYPE_INT, 360,
                               "height", G_TYPE_INT, 640,
                               "framerate", GST_TYPE_FRACTION, 15, 1,
                               NULL);

    link_ok = gst_element_link_filtered(element1, element2, caps);
    gst_caps_unref(caps);

    if (!link_ok)
    {
        g_warning("Failed to link element1 and element2 using video filter!");
    }

    return link_ok;
}

static gboolean link_elements_with_audio_filter(GstElement *element1, GstElement *element2, int sampleRate, int numChannels)
{
    gboolean link_ok;
    GstCaps *caps;

    caps = gst_caps_new_simple("audio/x-raw",
                               "rate", G_TYPE_INT, sampleRate,
                               "channels", G_TYPE_INT, numChannels,
                               NULL);

    link_ok = gst_element_link_filtered(element1, element2, caps);
    gst_caps_unref(caps);

    if (!link_ok)
    {
        g_warning("Failed to link element1 and element2 using audio filter!");
    }

    return link_ok;
}

/* This function will be called by the pad-added signal */
static void pad_added_handler(GstElement *src, GstPad *new_pad, CustomData *data)
{
    GstPad *video_sink_pad = gst_element_get_static_pad(data->video_queue, "sink");
    GstPad *audio_sink_pad = gst_element_get_static_pad(data->audio_queue, "sink");
    GstPadLinkReturn ret;
    GstCaps *new_pad_caps = NULL;
    GstStructure *new_pad_struct = NULL;
    const gchar *new_pad_type = NULL;

    g_print("Received new pad '%s' from '%s':\n", GST_PAD_NAME(new_pad), GST_ELEMENT_NAME(src));

    /* If our queues are already linked, we have nothing to do here */

    /* Check the new pad's type */
    new_pad_caps = gst_pad_get_current_caps(new_pad);
    new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
    new_pad_type = gst_structure_get_name(new_pad_struct);
    if (g_str_has_prefix(new_pad_type, "video/x-raw"))
    {
        if (gst_pad_is_linked(video_sink_pad))
        {
            g_print("We are already linked. Ignoring.\n");
            goto exit;
        }

        ret = gst_pad_link(new_pad, video_sink_pad);
    }
    else if (g_str_has_prefix(new_pad_type, "audio/x-raw"))
    {
        if (gst_pad_is_linked(audio_sink_pad))
        {
            g_print("We are already linked. Ignoring.\n");
            goto exit;
        }

        ret = gst_pad_link(new_pad, audio_sink_pad);
    }
    else
    {
        g_print("It has type '%s' which is not raw video/audio. Ignoring.\n", new_pad_type);
        goto exit;
    }

    if (GST_PAD_LINK_FAILED(ret))
    {
        g_print("Type is '%s' but link failed.\n", new_pad_type);
    }
    else
    {
        g_print("Link succeeded (type '%s').\n", new_pad_type);
    }

exit:
    /* Unreference the new pad's caps, if we got them */
    if (new_pad_caps != NULL)
        gst_caps_unref(new_pad_caps);

    /* Unreference the sink pads */
    gst_object_unref(video_sink_pad);
    gst_object_unref(audio_sink_pad);
}

int main(int argc, char *argv[])
{
    CustomData data;
    GstBus *bus;
    GstMessage *msg;

    GstPad *video_tee_flv_pad, *video_tee_mp4_pad;
    GstPad *video_flv_queue_sink_pad, *splitmuxsink_video_pad;

    GstPad *audio_tee_flv_pad, *audio_tee_mp4_pad;
    GstPad *audio_flv_queue_sink_pad, *splitmuxsink_audio_pad;

    GstPad *video_flv_queue_src_pad, *audio_flv_queue_src_pad;
    GstPad *flv_mux_video_pad, *flv_mux_audio_pad;

    /* Initialize GStreamer */
    gst_init(&argc, &argv);

    /* Create the elements */
    data.source = gst_element_factory_make("uridecodebin", "source");

    data.video_queue = gst_element_factory_make("queue", "video_queue");
    data.video_convert = gst_element_factory_make("videoconvert", "video_convert");
    data.face_blur = gst_element_factory_make("faceblur", "face_blur");
    data.video_convert2 = gst_element_factory_make("videoconvert", "video_convert2");
    data.x264_enc = gst_element_factory_make("x264enc", "x264_enc");
    data.video_tee = gst_element_factory_make("tee", "video_tee");
    data.video_flv_queue = gst_element_factory_make("queue", "video_flv_queue");

    data.audio_queue = gst_element_factory_make("queue", "audio_queue");
    data.audio_convert = gst_element_factory_make("audioconvert", "audio_convert");
    data.audio_resample = gst_element_factory_make("audioresample", "audio_resample");
    data.avenc_aac = gst_element_factory_make("fdkaacenc", "avenc_aac");
    data.audio_tee = gst_element_factory_make("tee", "audio_tee");
    data.audio_flv_queue = gst_element_factory_make("queue", "audio_flv_queue");

    data.flv_mux = gst_element_factory_make("flvmux", "flv_mux");
    data.flv_filesink = gst_element_factory_make("filesink", "flv_filesink");
    data.split_mux_sink = gst_element_factory_make("splitmuxsink", "split_mux_sink");
    data.gcs_sink = gst_element_factory_make("awss3sink", "gcs_sink");

    /* Create the empty pipeline */
    data.pipeline = gst_pipeline_new("test-pipeline");

    if (!data.pipeline || !data.source ||
        !data.video_queue || !data.video_convert || !data.face_blur || !data.video_convert2 || !data.x264_enc || !data.video_tee || !data.video_flv_queue ||
        !data.audio_queue || !data.audio_convert || !data.audio_resample || !data.avenc_aac || !data.audio_tee || !data.audio_flv_queue ||
        !data.flv_mux || !data.flv_filesink || !data.split_mux_sink || !data.gcs_sink)
    {
        g_printerr("Not all elements could be created.\n");
        return -1;
    }
    else
    {
        g_print("All elements created successfully.\n");
    }

    /* Configure elements */
    g_object_set(data.source, "uri", "add-here", NULL);
    g_object_set(data.x264_enc, "speed-preset", 2, "pass", 5, "bitrate", 1200, "key-int-max", 30, "quantizer", 22, NULL);
    g_object_set(data.face_blur, "scale-factor", 1.1, "profile", "/usr/share/opencv4/haarcascades/haarcascade_frontalface_alt.xml", NULL);

    g_object_set(data.avenc_aac, "rate-control", 1, "vbr-preset", 1, NULL);
    g_object_set(data.flv_mux, "streamable", true, "enforce-increasing-timestamps", false, NULL);
    g_object_set(data.flv_filesink, "location", "/home/ubuntu/vivek-personal/flvtest/output.flv", "sync", true, NULL);
    g_object_set(data.gcs_sink, "access-key", "add-here", "bucket", "livestream-recording-service-stage-bucket", "endpoint-uri", "https://storage.googleapis.com", "force-path-style", true, "region", "asia-southeast1", "secret-access-key", "add-here", "sync", true, NULL);
    g_object_set(data.split_mux_sink, "max-size-time", (guint64)SEGMENT_DURATION * GST_MSECOND, "send-keyframe-requests", true, "sink", data.gcs_sink, NULL);

    // Connect the format-location signal to generate dynamic filenames
    g_signal_connect(data.split_mux_sink, "format-location", G_CALLBACK(format_location_callback), data.gcs_sink);
    /* Connect to the pad-added signal */
    g_signal_connect(data.source, "pad-added", G_CALLBACK(pad_added_handler), &data);

    g_print("All elements configured successfully.\n");

    /* Link all elements that can be automatically linked because they have "Always" pads */
    gst_bin_add_many(GST_BIN(data.pipeline), data.source,
                     data.video_queue, data.video_convert, data.face_blur, data.video_convert2, data.x264_enc, data.video_tee, data.video_flv_queue,
                     data.audio_queue, data.audio_convert, data.audio_resample, data.avenc_aac, data.audio_tee, data.audio_flv_queue,
                     data.flv_mux, data.flv_filesink, data.split_mux_sink, NULL);

    if (gst_element_link_many(data.video_queue, data.video_convert, data.face_blur, NULL) != TRUE ||
        gst_element_link_many(data.face_blur, data.video_convert2, data.x264_enc, data.video_tee, NULL) != TRUE ||

        gst_element_link_many(data.audio_queue, data.audio_convert, data.audio_resample, NULL) != TRUE ||
        link_elements_with_audio_filter(data.audio_resample, data.avenc_aac, 16000, 1) != TRUE ||
        gst_element_link(data.avenc_aac, data.audio_tee) != TRUE ||

        gst_element_link_many(data.flv_mux, data.flv_filesink, NULL) != TRUE)
    {
        g_printerr("Elements could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
    else
    {
        g_print("All elements linked successfully.\n");
    }

    /* Manually link the video_tee, which has "Request" pads */
    video_tee_flv_pad = gst_element_request_pad_simple(data.video_tee, "src_%u");
    g_print("Obtained request pad %s for video_tee's flvmux branch.\n", gst_pad_get_name(video_tee_flv_pad));
    video_flv_queue_sink_pad = gst_element_get_static_pad(data.video_flv_queue, "sink");

    video_tee_mp4_pad = gst_element_request_pad_simple(data.video_tee, "src_%u");
    g_print("Obtained request pad %s for video_tee's mp4mux branch.\n", gst_pad_get_name(video_tee_mp4_pad));
    splitmuxsink_video_pad = gst_element_request_pad_simple(data.split_mux_sink, "video");
    g_print("Obtained request pad %s for splitmuxsink video branch.\n", gst_pad_get_name(splitmuxsink_video_pad));

    if (gst_pad_link(video_tee_flv_pad, video_flv_queue_sink_pad) != GST_PAD_LINK_OK ||
        gst_pad_link(video_tee_mp4_pad, splitmuxsink_video_pad) != GST_PAD_LINK_OK)
    {
        g_printerr("video_tee could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
    else
    {
        g_print("video_tee linked successfully.\n");
    }
    gst_object_unref(video_flv_queue_sink_pad);
    gst_object_unref(splitmuxsink_video_pad);

    /* Manually link the audio_tee, which has "Request" pads */
    audio_tee_flv_pad = gst_element_request_pad_simple(data.audio_tee, "src_%u");
    g_print("Obtained request pad %s for audio_tee's flvmux branch.\n", gst_pad_get_name(audio_tee_flv_pad));
    audio_flv_queue_sink_pad = gst_element_get_static_pad(data.audio_flv_queue, "sink");

    audio_tee_mp4_pad = gst_element_request_pad_simple(data.audio_tee, "src_%u");
    g_print("Obtained request pad %s for audio_tee's mp4mux branch.\n", gst_pad_get_name(audio_tee_mp4_pad));
    splitmuxsink_audio_pad = gst_element_request_pad_simple(data.split_mux_sink, "audio_%u");
    g_print("Obtained request pad %s for splitmuxsink audio branch.\n", gst_pad_get_name(splitmuxsink_audio_pad));

    if (gst_pad_link(audio_tee_flv_pad, audio_flv_queue_sink_pad) != GST_PAD_LINK_OK ||
        gst_pad_link(audio_tee_mp4_pad, splitmuxsink_audio_pad) != GST_PAD_LINK_OK)
    {
        g_printerr("audio_tee could not be linked.\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
    else
    {
        g_print("audio_tee linked successfully.\n");
    }
    gst_object_unref(audio_flv_queue_sink_pad);
    gst_object_unref(splitmuxsink_audio_pad);

    /* Manually link the flvmux which has "Request" pads */
    video_flv_queue_src_pad = gst_element_get_static_pad(data.video_flv_queue, "src");
    flv_mux_video_pad = gst_element_request_pad_simple(data.flv_mux, "video");
    g_print("Obtained request pad %s for flvmux video branch.\n", gst_pad_get_name(flv_mux_video_pad));

    audio_flv_queue_src_pad = gst_element_get_static_pad(data.audio_flv_queue, "src");
    flv_mux_audio_pad = gst_element_request_pad_simple(data.flv_mux, "audio");
    g_print("Obtained request pad %s for flvmux audio branch.\n", gst_pad_get_name(flv_mux_audio_pad));

    if (gst_pad_link(video_flv_queue_src_pad, flv_mux_video_pad) != GST_PAD_LINK_OK ||
        gst_pad_link(audio_flv_queue_src_pad, flv_mux_audio_pad) != GST_PAD_LINK_OK)
    {
        g_printerr("flvmux could not be linked!\n");
        gst_object_unref(data.pipeline);
        return -1;
    }
    else
    {
        g_print("flvmux linked successfully.\n");
    }
    gst_object_unref(video_flv_queue_src_pad);
    gst_object_unref(audio_flv_queue_src_pad);

    /* Start playing the pipeline */
    gst_element_set_state(data.pipeline, GST_STATE_PLAYING);

    /* Visualize the pipeline using GraphViz */
    gst_debug_bin_to_dot_file(GST_BIN(data.pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-faceblur");

    /* Wait until error or EOS */
    bus = gst_element_get_bus(data.pipeline);
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    /* Release the request pads from the video_tee, and unref them */
    gst_element_release_request_pad(data.video_tee, video_tee_flv_pad);
    gst_element_release_request_pad(data.video_tee, video_tee_mp4_pad);
    gst_object_unref(video_tee_flv_pad);
    gst_object_unref(video_tee_mp4_pad);

    /* Release the request pads from the audio_tee, and unref them */
    gst_element_release_request_pad(data.audio_tee, audio_tee_flv_pad);
    gst_element_release_request_pad(data.audio_tee, audio_tee_mp4_pad);
    gst_object_unref(audio_tee_flv_pad);
    gst_object_unref(audio_tee_mp4_pad);

    /* Release the request pads from flvmux, and unref them */
    gst_element_release_request_pad(data.flv_mux, flv_mux_video_pad);
    gst_element_release_request_pad(data.flv_mux, flv_mux_audio_pad);
    gst_object_unref(flv_mux_video_pad);
    gst_object_unref(flv_mux_audio_pad);

    /* Free resources */
    if (msg != NULL)
        gst_message_unref(msg);
    gst_object_unref(bus);
    gst_element_set_state(data.pipeline, GST_STATE_NULL);

    gst_object_unref(data.pipeline);
    return 0;
}
