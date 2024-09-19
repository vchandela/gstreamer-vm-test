#include <gst/gst.h>

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
  GstElement *pipeline, *audio_source, *tee, *audio_queue, *audio_convert, *audio_resample, *avenc_aac, *audio_sink;
  GstElement *video_source, *video_queue, *video_convert, *x264_enc, *video_sink;
  GstBus *bus;
  GstMessage *msg;

  /* Initialize GStreamer */
  gst_init (&argc, &argv);

  /* Create the elements */
  audio_source = gst_element_factory_make ("audiotestsrc", "audio_source");
  audio_queue = gst_element_factory_make ("queue", "audio_queue");
  audio_convert = gst_element_factory_make ("audioconvert", "audio_convert");
  audio_resample = gst_element_factory_make ("audioresample", "audio_resample");
  avenc_aac = gst_element_factory_make ("avenc_aac", "avenc_aac");
  audio_sink = gst_element_factory_make ("autoaudiosink", "audio_sink");

  video_source = gst_element_factory_make ("videotestsrc", "video_source");
  video_queue = gst_element_factory_make ("queue", "video_queue");
  video_convert = gst_element_factory_make ("videoconvert", "csp");
  x264_enc = gst_element_factory_make ("x264enc", "x264_enc");
  video_sink = gst_element_factory_make ("autovideosink", "video_sink");

  /* Create the empty pipeline */
  pipeline = gst_pipeline_new ("test-pipeline");

  if (!pipeline || !audio_source || !audio_queue || !audio_convert || !audio_resample || !avenc_aac || !audio_sink ||
      !video_source || !video_queue || !video_convert || !x264_enc || !video_sink) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  } else {
    g_print ("All elements created successfully.\n");
  }

  /* Configure elements */
  g_object_set (x264_enc, "speed-preset", 1, "bitrate", 128, NULL);
  g_object_set (avenc_aac, "bitrate", 256, NULL);

  /* Link all elements that can be automatically linked because they have "Always" pads */
  gst_bin_add_many (GST_BIN (pipeline), audio_source, audio_queue, audio_convert, audio_resample, avenc_aac, audio_sink,
      video_source, video_queue, video_convert, x264_enc, video_sink, NULL);

  if (link_elements_with_audio_filter (audio_source, audio_queue, 48000, 2) != TRUE ||
    gst_element_link_many (audio_queue, audio_convert, audio_resample, NULL) != TRUE ||
    link_elements_with_audio_filter (audio_resample, avenc_aac, 16000, 1) != TRUE ||
    gst_element_link_many (avenc_aac, audio_sink, NULL) != TRUE ||

    link_elements_with_video_filter (video_source, video_queue) != TRUE ||
    gst_element_link_many (video_queue, video_convert, x264_enc, video_sink, NULL) != TRUE) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (pipeline);
    return -1;
  } else {
    g_print ("All elements linked successfully.\n");
  }

  /* Start playing the pipeline */
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

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