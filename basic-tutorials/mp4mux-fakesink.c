#include <gst/gst.h>
#include <stdbool.h>

int main(int argc, char *argv[]) {
    GstElement *pipeline, *video_source, *video_queue, *video_convert, *x264_enc, *video_mp4mux_queue;
    GstElement *mp4_mux, *fake_sink;
    
    GstBus *bus;
    GstMessage *msg;

    GstPad *video_mp4mux_queue_src_pad;
    GstPad *mp4mux_video_pad;

    /* Initialize GStreamer */
    gst_init (&argc, &argv);

    /* Create the elements */
    video_source = gst_element_factory_make ("videotestsrc", "video_source");
    video_queue = gst_element_factory_make ("queue", "video_queue");
    video_convert = gst_element_factory_make ("videoconvert", "csp");
    x264_enc = gst_element_factory_make ("x264enc", "x264_enc");
    video_mp4mux_queue = gst_element_factory_make ("queue", "video_mp4_queue");

    mp4_mux = gst_element_factory_make ("mp4mux", "mp4_mux");
    fake_sink = gst_element_factory_make ("fakesink", "fake_sink");

    /* Create the empty pipeline */
    pipeline = gst_pipeline_new ("test-pipeline");

    if (!pipeline || !video_source || !video_queue || !video_convert || !x264_enc || !video_mp4mux_queue || 
    !mp4_mux || !fake_sink) {
        g_printerr ("Not all elements could be created.\n");
        return -1;
    } else {
        g_print ("All elements created.\n");
    }

    /* Configure elements */
    g_object_set (x264_enc, "speed-preset", 1, "bitrate", 128, NULL);
    g_object_set (fake_sink, "sync", true, NULL);

    g_print ("All elements configured successfully.\n");

    /* Link all elements that can be automatically linked because they have "Always" pads */
    gst_bin_add_many (GST_BIN (pipeline), video_source, video_queue, video_convert, x264_enc, video_mp4mux_queue, 
    mp4_mux, fake_sink, NULL);

    if (gst_element_link_many (video_source, video_queue, video_convert, x264_enc, video_mp4mux_queue, NULL) != TRUE ||

    gst_element_link_many (mp4_mux, fake_sink, NULL) != TRUE) {
        g_printerr ("Elements could not be linked.\n");
        gst_object_unref (pipeline);
        return -1;
    } else {
        g_print ("All elements linked.\n");
    }

    /* Manually link the mp4mux which has "Request" pads */
    video_mp4mux_queue_src_pad = gst_element_get_static_pad (video_mp4mux_queue, "src");
    mp4mux_video_pad = gst_element_request_pad_simple (mp4_mux, "video_%u");
    g_print ("Obtained request pad %s for mp4mux video branch.\n", gst_pad_get_name (mp4mux_video_pad));

    if (gst_pad_link (video_mp4mux_queue_src_pad, mp4mux_video_pad) != GST_PAD_LINK_OK) {
        g_printerr ("mp4mux could not be linked!\n");
        gst_object_unref (pipeline);
        return -1;
    } else {
        g_print ("mp4mux linked successfully.\n");
    }
    gst_object_unref (video_mp4mux_queue_src_pad);

    /* Start playing the pipeline */
    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    /* Visualize the pipeline using GraphViz */
    gst_debug_bin_to_dot_file(GST_BIN(pipeline), GST_DEBUG_GRAPH_SHOW_ALL, "pipeline-mp4mux-fakesink");

    /* Wait until error or EOS */
    bus = gst_element_get_bus (pipeline);
    msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

    /* Release the request pads from mp4mux, and unref them */
    gst_element_release_request_pad (mp4_mux, mp4mux_video_pad);
    gst_object_unref (mp4mux_video_pad);

    /* Free resources */
    if (msg != NULL)
        gst_message_unref (msg);
    gst_object_unref (bus);
    gst_element_set_state (pipeline, GST_STATE_NULL);

    gst_object_unref (pipeline);
    return 0;
}
