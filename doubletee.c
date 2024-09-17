#include <gst/gst.h>

static gboolean link_elements_with_video_filter (GstElement *element1, GstElement *element2)
{
  gboolean link_ok;
  GstCaps *caps;

  caps = gst_caps_new_simple ("video/x-raw",
          "width", G_TYPE_INT, 720,
          "height", G_TYPE_INT, 1280,
          "framerate", GST_TYPE_FRACTION, 15, 1,
          NULL);

  link_ok = gst_element_link_filtered (element1, element2, caps);
  gst_caps_unref (caps);

  if (!link_ok) {
    g_warning ("Failed to link element1 and element2!");
  }

  return link_ok;
}

int main(int argc, char *argv[]) {
  GstElement *pipeline;
  GstElement *video_source, *video_convert, *x264_enc, *video_tee, *video_flv_queue, *video_mp4_queue;
//   GstElement *audio_source, *audio_convert, *audio_resample, *avenc_aac, *audio_tee, *audio_flv_queue, *audio_mp4_queue;
  GstElement *flv_mux, *flv_filesink, *splitmuxsink;

  GstBus *bus;
  GstMessage *msg;
  GstPad *flv_mux_video_pad;
  GstPad *x264enc_pad;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the elements */
  video_source = gst_element_factory_make ("videotestsrc", "video_source");
  video_convert = gst_element_factory_make ("videoconvert", "video_convert");
  x264_enc = gst_element_factory_make ("x264enc", "x264_enc");
//   video_tee = gst_element_factory_make ("tee", "video_tee");
//   video_flv_queue = gst_element_factory_make ("queue", "video_flv_queue");
//   video_mp4_queue = gst_element_factory_make ("queue", "video_mp4_queue");

//   audio_source = gst_element_factory_make ("audiotestsrc", "audio_source");
//   audio_convert = gst_element_factory_make ("audioconvert", "audio_convert");
//   audio_resample = gst_element_factory_make ("audioresample", "audio_resample");
//   avenc_aac = gst_element_factory_make ("avenc_aac", "avenc_aac");
//   audio_tee = gst_element_factory_make ("tee", "audio_tee");
//   audio_flv_queue = gst_element_factory_make ("queue", "audio_flv_queue");
//   audio_mp4_queue = gst_element_factory_make ("queue", "audio_mp4_queue");

  flv_mux = gst_element_factory_make ("flvmux", "flv_mux");
  flv_filesink = gst_element_factory_make ("filesink", "flv_filesink");
//   splitmuxsink = gst_element_factory_make ("splitmuxsink", "splitmuxsink");

  /* Create the empty pipeline */
  pipeline = gst_pipeline_new ("test-pipeline");

  if (!pipeline || !video_source || !video_convert || !x264_enc || !flv_mux || !flv_filesink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }

  /* Configure elements */
  g_object_set (x264_enc, "speed-preset", 1, "bitrate", 128, NULL);
  g_object_set (flv_filesink, "location", "/Users/vivekchandela/Documents/flvtest/output.flv", NULL);

  /* Link all elements that can be automatically linked because they have "Always" pads */
  gst_bin_add_many (GST_BIN (pipeline), video_source, video_convert, x264_enc, flv_mux, flv_filesink, NULL);
  if (link_elements_with_video_filter (video_source, video_convert) != TRUE ||
      gst_element_link_many (video_convert, x264_enc, NULL) != TRUE ||
      gst_element_link_many (flv_mux, flv_filesink, NULL) != TRUE
      ) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (pipeline);
    return -1;
  }

  /* Manually link the flvmux, which has "Request" pads */
  x264enc_pad = gst_element_get_static_pad (x264_enc, "src");
  flv_mux_video_pad = gst_element_request_pad_simple (flv_mux, "video");
  g_print ("Obtained request pad %s for flvmux video branch.\n", gst_pad_get_name (flv_mux_video_pad));
  if (gst_pad_link (x264enc_pad, flv_mux_video_pad) != GST_PAD_LINK_OK) {
    g_printerr ("Failed to link x264enc to flvmux!\n");
    gst_object_unref (pipeline);
    return -1;
  } else {
    g_print ("Linked x264enc to flvmux.\n");
  }
  gst_object_unref (x264enc_pad);

  /* Manually link the Tee, which has "Request" pads */
//   tee_audio_pad = gst_element_request_pad_simple (tee, "src_%u");
//   g_print ("Obtained request pad %s for audio branch.\n", gst_pad_get_name (tee_audio_pad));
//   queue_audio_pad = gst_element_get_static_pad (audio_queue, "sink");
//   tee_video_pad = gst_element_request_pad_simple (tee, "src_%u");
//   g_print ("Obtained request pad %s for video branch.\n", gst_pad_get_name (tee_video_pad));
//   queue_video_pad = gst_element_get_static_pad (video_queue, "sink");
//   if (gst_pad_link (tee_audio_pad, queue_audio_pad) != GST_PAD_LINK_OK ||
//       gst_pad_link (tee_video_pad, queue_video_pad) != GST_PAD_LINK_OK) {
//     g_printerr ("Tee could not be linked.\n");
//     gst_object_unref (pipeline);
//     return -1;
//   }
//   gst_object_unref (queue_audio_pad);
//   gst_object_unref (queue_video_pad);

  /* Start playing the pipeline */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  /* Wait until error or EOS */
  bus = gst_element_get_bus (pipeline);
  msg = gst_bus_timed_pop_filtered (bus, GST_CLOCK_TIME_NONE, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  /* Release the request pads from the Tee, and unref them */
//   gst_element_release_request_pad (tee, tee_audio_pad);
//   gst_element_release_request_pad (tee, tee_video_pad);
//   gst_object_unref (tee_audio_pad);
//   gst_object_unref (tee_video_pad);

  /* Free resources */
  if (msg != NULL)
    gst_message_unref (msg);
  gst_object_unref (bus);
  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (pipeline);
  return 0;
}