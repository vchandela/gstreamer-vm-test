#include <gst/gst.h>
#include <stdbool.h>    

static gboolean link_elements_with_video_filter (GstElement *element1, GstElement *element2)
{
    gboolean link_ok;
    GstCaps *caps;

    caps = gst_caps_new_simple ("video/x-raw",
            "format", G_TYPE_STRING, "I420",
            "width", G_TYPE_INT, 720,
            "height", G_TYPE_INT, 1280,
            "framerate", GST_TYPE_FRACTION, 15, 1,
            NULL);

    link_ok = gst_element_link_filtered (element1, element2, caps);
    gst_caps_unref (caps);

    if (!link_ok) {
        g_warning ("Failed to link element1 and element2 using video filter!");
    }

    return link_ok;
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
    GstElement *video_source, *video_queue, *video_convert, *x264_enc, *video_tee, *video_flv_queue;
    GstElement *audio_source, *audio_queue, *audio_convert, *audio_resample, *avenc_aac, *audio_tee, *audio_flv_queue;
    GstElement *flv_mux, *flv_filesink, *split_mux_sink;

    GstBus *bus;
    GstMessage *msg;
    
    GstPad *video_tee_flv_pad, *video_tee_mp4_pad;
    GstPad *video_flv_queue_sink_pad, *splitmuxsink_video_pad;

    GstPad *audio_tee_flv_pad, *audio_tee_mp4_pad;
    GstPad *audio_flv_queue_sink_pad, *splitmuxsink_audio_pad;

    GstPad *video_flv_queue_src_pad, *audio_flv_queue_src_pad;
    GstPad *flv_mux_video_pad, *flv_mux_audio_pad;

    GstStructure *sink_props;

    /* Initialize GStreamer */
    gst_init (&argc, &argv);

    /* Create the elements */
    video_source = gst_element_factory_make ("videotestsrc", "video_source");
    video_queue = gst_element_factory_make ("queue", "video_queue");
    video_convert = gst_element_factory_make ("videoconvert", "video_convert");
    x264_enc = gst_element_factory_make ("x264enc", "x264_enc");
    video_tee = gst_element_factory_make ("tee", "video_tee");
    video_flv_queue = gst_element_factory_make ("queue", "video_flv_queue");

    audio_source = gst_element_factory_make ("audiotestsrc", "audio_source");
    audio_queue = gst_element_factory_make ("queue", "audio_queue");
    audio_convert = gst_element_factory_make ("audioconvert", "audio_convert");
    audio_resample = gst_element_factory_make ("audioresample", "audio_resample");
    avenc_aac = gst_element_factory_make ("avenc_aac", "avenc_aac");
    audio_tee = gst_element_factory_make ("tee", "audio_tee");
    audio_flv_queue = gst_element_factory_make ("queue", "audio_flv_queue");

    flv_mux = gst_element_factory_make ("flvmux", "flv_mux");
    flv_filesink = gst_element_factory_make ("filesink", "flv_filesink");
    split_mux_sink = gst_element_factory_make ("splitmuxsink", "split_mux_sink");

    /* Create the empty pipeline */
    pipeline = gst_pipeline_new ("test-pipeline");

    if (!pipeline || !video_source || !video_queue || !video_convert || !x264_enc || !video_tee || !video_flv_queue ||
    !audio_source || !audio_queue || !audio_convert || !audio_resample || !avenc_aac || !audio_tee || !audio_flv_queue ||
    !flv_mux || !flv_filesink || !split_mux_sink) {
        g_printerr ("Not all elements could be created.\n");
        return -1;
    } else {
        g_print ("All elements created successfully.\n");
    }

    /* Configure elements */
    g_object_set (x264_enc, "speed-preset", 1, "bitrate", 128, NULL);
    g_object_set (avenc_aac, "bitrate", 256, NULL);
    g_object_set (flv_mux, "streamable", true, "enforce-increasing-timestamps", false, NULL);
    g_object_set (flv_filesink, "location", "/Users/vivekchandela/Documents/flvtest/output.flv", "sync", true, NULL);
    g_object_set(split_mux_sink, "location", "/Users/vivekchandela/Documents/mp4test/chunk%02d.mp4", "max-size-time", 15000000000, "async-finalize", true, "send-keyframe-requests", true, "sink-properties", sink_props, NULL);

    g_print ("All elements configured successfully.\n");
  
    /* Link all elements that can be automatically linked because they have "Always" pads */
    /* Adding caps filter between video_source and video_convert */
    gst_bin_add_many (GST_BIN (pipeline), video_source, video_queue, video_convert, x264_enc, video_tee, video_flv_queue,
    audio_source, audio_queue, audio_convert, audio_resample, avenc_aac, audio_tee, audio_flv_queue,
    flv_mux, flv_filesink, split_mux_sink, NULL);

    if (link_elements_with_video_filter (video_source, video_queue) != TRUE ||
        gst_element_link_many (video_queue, video_convert, x264_enc, video_tee, NULL) != TRUE ||

        link_elements_with_audio_filter (audio_source, audio_queue, 48000, 2) != TRUE ||
        gst_element_link_many (audio_queue, audio_convert, audio_resample, NULL) != TRUE ||
        link_elements_with_audio_filter (audio_resample, avenc_aac, 16000, 1) != TRUE ||
        gst_element_link_many (avenc_aac, audio_tee, NULL) != TRUE ||
        
        gst_element_link_many (flv_mux, flv_filesink, NULL) != TRUE
        ) {
        g_printerr ("Elements could not be linked.\n");
        gst_object_unref (pipeline);
        return -1;
    } else {
        g_print ("All elements linked successfully.\n");
    }

    /* Manually link the video_tee, which has "Request" pads */
    video_tee_flv_pad = gst_element_request_pad_simple (video_tee, "src_%u");
    g_print ("Obtained request pad %s for video_tee's flvmux branch.\n", gst_pad_get_name (video_tee_flv_pad));
    video_flv_queue_sink_pad = gst_element_get_static_pad (video_flv_queue, "sink");
    
    video_tee_mp4_pad = gst_element_request_pad_simple (video_tee, "src_%u");
    g_print ("Obtained request pad %s for video_tee's mp4mux branch.\n", gst_pad_get_name (video_tee_mp4_pad));
    splitmuxsink_video_pad = gst_element_request_pad_simple (split_mux_sink, "video");
    g_print ("Obtained request pad %s for splitmuxsink video branch.\n", gst_pad_get_name (splitmuxsink_video_pad));

    if (gst_pad_link (video_tee_flv_pad, video_flv_queue_sink_pad) != GST_PAD_LINK_OK ||
        gst_pad_link (video_tee_mp4_pad, splitmuxsink_video_pad) != GST_PAD_LINK_OK) {
        g_printerr ("video_tee could not be linked.\n");
        gst_object_unref (pipeline);
        return -1;
    } else {
        g_print ("video_tee linked successfully.\n");
    }
    gst_object_unref (video_flv_queue_sink_pad);
    gst_object_unref (splitmuxsink_video_pad);

    /* Manually link the audio_tee, which has "Request" pads */
    audio_tee_flv_pad = gst_element_request_pad_simple (audio_tee, "src_%u");
    g_print ("Obtained request pad %s for audio_tee's flvmux branch.\n", gst_pad_get_name (audio_tee_flv_pad));
    audio_flv_queue_sink_pad = gst_element_get_static_pad (audio_flv_queue, "sink");
    
    audio_tee_mp4_pad = gst_element_request_pad_simple (audio_tee, "src_%u");
    g_print ("Obtained request pad %s for audio_tee's mp4mux branch.\n", gst_pad_get_name (audio_tee_mp4_pad));
    splitmuxsink_audio_pad = gst_element_request_pad_simple (split_mux_sink, "audio_%u");
    g_print ("Obtained request pad %s for splitmuxsink audio branch.\n", gst_pad_get_name (splitmuxsink_audio_pad));

    if (gst_pad_link (audio_tee_flv_pad, audio_flv_queue_sink_pad) != GST_PAD_LINK_OK ||
        gst_pad_link (audio_tee_mp4_pad, splitmuxsink_audio_pad) != GST_PAD_LINK_OK) {
        g_printerr ("audio_tee could not be linked.\n");
        gst_object_unref (pipeline);
        return -1;
    } else {
        g_print ("audio_tee linked successfully.\n");
    }
    gst_object_unref (audio_flv_queue_sink_pad);
    gst_object_unref (splitmuxsink_audio_pad);

    /* Manually link the flvmux which has "Request" pads */
    video_flv_queue_src_pad = gst_element_get_static_pad (video_flv_queue, "src");
    flv_mux_video_pad = gst_element_request_pad_simple (flv_mux, "video");
    g_print ("Obtained request pad %s for flvmux video branch.\n", gst_pad_get_name (flv_mux_video_pad));

    audio_flv_queue_src_pad = gst_element_get_static_pad (audio_flv_queue, "src");
    flv_mux_audio_pad = gst_element_request_pad_simple (flv_mux, "audio");
    g_print ("Obtained request pad %s for flvmux audio branch.\n", gst_pad_get_name (flv_mux_audio_pad));

    if (gst_pad_link (video_flv_queue_src_pad, flv_mux_video_pad) != GST_PAD_LINK_OK ||
    gst_pad_link (audio_flv_queue_src_pad, flv_mux_audio_pad) != GST_PAD_LINK_OK) {
        g_printerr ("flvmux could not be linked!\n");
        gst_object_unref (pipeline);
        return -1;
    } else {
        g_print ("flvmux linked successfully.\n");
    }
    gst_object_unref (video_flv_queue_src_pad);
    gst_object_unref (audio_flv_queue_src_pad);

    /* Start playing the pipeline */
    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    /* Visualize the pipeline using GraphViz */
    gst_debug_bin_to_dot_file(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-doubletee-doublequeue");

    /* Wait until error or EOS */
    bus = gst_element_get_bus (pipeline);
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    /* Release the request pads from the video_tee, and unref them */
    gst_element_release_request_pad (video_tee, video_tee_flv_pad);
    gst_element_release_request_pad (video_tee, video_tee_mp4_pad);
    gst_object_unref (video_tee_flv_pad);
    gst_object_unref (video_tee_mp4_pad);

    /* Release the request pads from the audio_tee, and unref them */
    gst_element_release_request_pad (audio_tee, audio_tee_flv_pad);
    gst_element_release_request_pad (audio_tee, audio_tee_mp4_pad);
    gst_object_unref (audio_tee_flv_pad);
    gst_object_unref (audio_tee_mp4_pad);

    /* Release the request pads from flvmux, and unref them */
    gst_element_release_request_pad (flv_mux, flv_mux_video_pad);
    gst_element_release_request_pad (flv_mux, flv_mux_audio_pad);
    gst_object_unref (flv_mux_video_pad);
    gst_object_unref (flv_mux_audio_pad);

    /* Free resources */
    if (msg != NULL)
        gst_message_unref (msg);
    gst_object_unref (bus);
    gst_element_set_state (pipeline, GST_STATE_NULL);

    gst_object_unref (pipeline);
    return 0;
}
