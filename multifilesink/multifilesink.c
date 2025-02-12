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
    GstElement *audio_source, *audio_queue, *audio_convert, *audio_resample, *lame_mp3_enc;
    GstElement *multi_file_sink;

    GstBus *bus;
    GstMessage *msg;
    
    /* Initialize GStreamer */
    gst_init (&argc, &argv);

    /* Create the elements */
    audio_source = gst_element_factory_make ("audiotestsrc", "audio_source");
    audio_queue = gst_element_factory_make ("queue", "audio_queue");
    audio_convert = gst_element_factory_make ("audioconvert", "audio_convert");
    audio_resample = gst_element_factory_make ("audioresample", "audio_resample");
    lame_mp3_enc = gst_element_factory_make ("lamemp3enc", "lame_mp3_enc");
    multi_file_sink = gst_element_factory_make ("multifilesink", "multi_file_sink");

    /* Create the empty pipeline */
    pipeline = gst_pipeline_new ("test-pipeline");

    if (!pipeline || !audio_source || !audio_queue || !audio_convert || !audio_resample || !lame_mp3_enc ||
    !multi_file_sink) {
        g_printerr ("Not all elements could be created.\n");
        return -1;
    } else {
        g_print ("All elements created successfully.\n");
    }

    /* Configure elements */
    g_object_set (audio_source, "is-live", true, NULL);
    g_object_set (lame_mp3_enc, "bitrate", 44, NULL);
    g_object_set(multi_file_sink, "location", "/home/ubuntu/gstreamer-vm-test/multifilesink/chunk%02d.mp3", "max-file-duration", 15000000000, "next-file", 5, NULL);

    g_print ("All elements configured successfully.\n");
  
    /* Link all elements that can be automatically linked because they have "Always" pads */
    /* Adding caps filter wherever needed */
    gst_bin_add_many (GST_BIN (pipeline), audio_source, audio_queue, audio_convert, audio_resample, lame_mp3_enc,
    multi_file_sink, NULL);

    if (link_elements_with_audio_filter (audio_source, audio_queue, 48000, 2) != TRUE ||
        gst_element_link_many (audio_queue, audio_convert, audio_resample, NULL) != TRUE ||
        link_elements_with_audio_filter (audio_resample, lame_mp3_enc, 16000, 1) != TRUE ||
        gst_element_link_many (lame_mp3_enc, multi_file_sink, NULL) != TRUE) {
        g_printerr ("Elements could not be linked.\n");
        gst_object_unref (pipeline);
        return -1;
    } else {
        g_print ("All elements linked successfully.\n");
    }

    /* Start playing the pipeline */
    GstStateChangeReturn ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);

    if (ret == GST_STATE_CHANGE_FAILURE) {
      g_error("Failed to start the pipeline\n");
    } else {
      g_print("Pipeline started successfully\n");
    }


    /* Visualize the pipeline using GraphViz */
    gst_debug_bin_to_dot_file(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-multifilesink");

    /* Wait until error or EOS */
    bus = gst_element_get_bus (pipeline);
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    /* Free resources */
    if (msg != NULL)
        gst_message_unref (msg);
    gst_object_unref (bus);
    gst_element_set_state (pipeline, GST_STATE_NULL);

    gst_object_unref (pipeline);
    return 0;
}
