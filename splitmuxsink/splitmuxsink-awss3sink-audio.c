#include <gst/gst.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>

#define SEGMENT_DURATION 15000 // Duration for each segment in milliseconds

// Function to get the current time in milliseconds since the Unix epoch
static long long current_time_millis() {
    struct timeval time_now;
    gettimeofday(&time_now, NULL);
    return (long long)(time_now.tv_sec) * 1000 + (long long)(time_now.tv_usec) / 1000;
}

static gchar* format_location_callback(GstElement *splitmuxsink, guint fragment_id, gpointer user_data) {
    // Get the (Unix epoch time) for start and end time
    long long start_time = current_time_millis();
    long long end_time = start_time + SEGMENT_DURATION;

    gchar *filename = g_strdup_printf("vm2/%lld_%lld.mp4", start_time, end_time);

    GstElement *gcs_sink = GST_ELEMENT(user_data); // Retrieve gcs_sink passed via user_data
    if (gcs_sink) {
        g_object_set(gcs_sink, "key", filename, NULL);  // Set the key dynamically
    }

    return filename;
}

static gboolean link_elements_with_audio_filter (GstElement *element1, GstElement *element2, int sampleRate, int numChannels)
{
    gboolean link_ok;
    GstCaps *caps;

    caps = gst_caps_new_simple ("audio/x-raw",
            "rate", G_TYPE_INT, sampleRate,
            "channels", G_TYPE_INT, numChannels,
            NULL);

    link_ok = gst_element_link_filtered (element1, element2, caps);
    gst_caps_unref (caps);

    if (!link_ok) {
        g_warning ("Failed to link element1 and element2 using audio filter!");
    }

    return link_ok;
}

int main(int argc, char *argv[]) {
    GstElement *pipeline;
    GstElement *audio_source, *audio_queue, *audio_convert, *audio_resample, *avenc_aac;
    GstElement *split_mux_sink, *gcs_sink;

    GstBus *bus;
    GstMessage *msg;

    GstPad *x264enc_src_pad, *avenc_aac_src_pad;
    GstPad *splitmuxsink_video_pad, *splitmuxsink_audio_pad;

    /* Initialize GStreamer */
    gst_init (&argc, &argv);

    /* Create the elements */
    audio_source = gst_element_factory_make ("audiotestsrc", "audio_source");
    audio_queue = gst_element_factory_make ("queue", "audio_queue");
    audio_convert = gst_element_factory_make ("audioconvert", "audio_convert");
    audio_resample = gst_element_factory_make ("audioresample", "audio_resample");
    avenc_aac = gst_element_factory_make ("fdkaacenc", "avenc_aac");

    split_mux_sink = gst_element_factory_make ("splitmuxsink", "split_mux_sink");
    gcs_sink = gst_element_factory_make ("awss3sink", "gcs_sink");

    /* Create the empty pipeline */
    pipeline = gst_pipeline_new ("test-pipeline");

    if (!pipeline || !audio_source || !audio_queue || !audio_convert || !audio_resample || !avenc_aac || !split_mux_sink || !gcs_sink) {
        g_printerr ("Not all elements could be created.\n");
        return -1;
    } else {
        g_print ("All elements created successfully.\n");
    }

    /* Configure elements */
    g_object_set (avenc_aac, "bitrate", 256, NULL);
    g_object_set (gcs_sink, "access-key", "add-here", "bucket", "add-here", "endpoint-uri", "https://storage.googleapis.com", "force-path-style", true, "region", "add-here", "secret-access-key", "add-here", "sync", true,  NULL);
    g_object_set(split_mux_sink, "max-size-time", (guint64)SEGMENT_DURATION * GST_MSECOND, "send-keyframe-requests", true, "sink", gcs_sink, NULL);

    // Connect the format-location signal to generate dynamic filenames
    g_signal_connect(split_mux_sink, "format-location", G_CALLBACK(format_location_callback), gcs_sink);

    g_print ("All elements configured successfully.\n");

    /* Link all elements that can be automatically linked because they have "Always" pads */
    /* Adding caps filter between video_source and video_convert */
    gst_bin_add_many (GST_BIN (pipeline), audio_source, audio_queue, audio_convert, audio_resample, avenc_aac,
    split_mux_sink, NULL);

    if (link_elements_with_audio_filter (audio_source, audio_queue, 48000, 2) != TRUE ||
        gst_element_link_many (audio_queue, audio_convert, audio_resample, NULL) != TRUE ||
        link_elements_with_audio_filter (audio_resample, avenc_aac, 16000, 1) != TRUE
        ) {
        g_printerr ("Elements could not be linked.\n");
        gst_object_unref (pipeline);
        return -1;
    } else {
        g_print ("All elements linked successfully.\n");
    }

    /* Manually link the splitmuxsink which has "Request" pads */
    avenc_aac_src_pad = gst_element_get_static_pad (avenc_aac, "src");
    splitmuxsink_audio_pad = gst_element_request_pad_simple (split_mux_sink, "audio_%u");
    g_print ("Obtained request pad %s for splitmuxsink audio branch.\n", gst_pad_get_name (splitmuxsink_audio_pad));

    if (gst_pad_link (avenc_aac_src_pad, splitmuxsink_audio_pad) != GST_PAD_LINK_OK) {
        g_printerr ("splitmuxsink could not be linked!\n");
        gst_object_unref (pipeline);
        return -1;
    } else {
        g_print ("splitmuxsink linked successfully.\n");
    }
    gst_object_unref (avenc_aac_src_pad);

    /* Start playing the pipeline */
    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    /* Visualize the pipeline using GraphViz */
    gst_debug_bin_to_dot_file(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-splitmuxsink-awss3sink-audio");

    /* Wait until error or EOS */
    bus = gst_element_get_bus (pipeline);
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    /* Release the request pads from splitmuxsink, and unref them */
    gst_element_release_request_pad (split_mux_sink, splitmuxsink_audio_pad);
    gst_object_unref (splitmuxsink_audio_pad);

    /* Free resources */
    if (msg != NULL)
        gst_message_unref (msg);
    gst_object_unref (bus);
    gst_element_set_state (pipeline, GST_STATE_NULL);

    gst_object_unref (pipeline);
    return 0;
}
