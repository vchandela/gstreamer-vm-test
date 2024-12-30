#include <gst/gst.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>

#define SEGMENT_DURATION 120000 // Duration for each segment in milliseconds
volatile gboolean terminate = FALSE;
volatile gboolean eos = FALSE;

typedef struct {
    GstElement *pipeline;
    GstElement *video_source;
    GstElement *video_queue;
    GstElement *video_convert;
    GstElement *x264_enc;
    GstElement *audio_source;
    GstElement *audio_queue;
    GstElement *audio_convert;
    GstElement *audio_resample;
    GstElement *avenc_aac;
    GstElement *split_mux_sink;
    GstElement *gcs_sink;
    GstPad *x264enc_src_pad;
    GstPad *avenc_aac_src_pad;
    GstPad *splitmuxsink_video_pad;
    GstPad *splitmuxsink_audio_pad;
    GMutex lock;
} MediaPush;

// Signal handler for SIGINT
void signal_handler(int signal) {
  if (signal == SIGINT) {
    g_print("Received SIGINT terminating\n");
    g_print("terminate: %s\n", terminate ? "TRUE" : "FALSE");
    terminate = TRUE;
    g_print("terminate: %s\n", terminate ? "TRUE" : "FALSE");
  }
}

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

    gchar *filename = g_strdup_printf("vm/%lld_%lld.mp4", start_time, end_time);

    GstElement *gcs_sink = GST_ELEMENT(user_data); // Retrieve gcs_sink passed via user_data
    if (gcs_sink) {
        g_object_set(gcs_sink, "key", filename, NULL);  // Set the key dynamically
    }

    return filename;
}

static gboolean link_elements_with_video_filter (GstElement *element1, GstElement *element2)
{
    gboolean link_ok;
    GstCaps *caps;

    caps = gst_caps_new_simple ("video/x-raw",
            "format", G_TYPE_STRING, "I420",
            "width", G_TYPE_INT, 360,
            "height", G_TYPE_INT, 640,
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

gboolean run_splitmuxsink(MediaPush *self) {
    /* Initialize GStreamer */
    gst_init (NULL, NULL);

    /* Create the elements */
    self->video_source = gst_element_factory_make ("videotestsrc", "video_source");
    self->video_queue = gst_element_factory_make ("queue", "video_queue");
    self->video_convert = gst_element_factory_make ("videoconvert", "video_convert");
    self->x264_enc = gst_element_factory_make ("x264enc", "x264_enc");

    self->audio_source = gst_element_factory_make ("audiotestsrc", "audio_source");
    self->audio_queue = gst_element_factory_make ("queue", "audio_queue");
    self->audio_convert = gst_element_factory_make ("audioconvert", "audio_convert");
    self->audio_resample = gst_element_factory_make ("audioresample", "audio_resample");
    self->avenc_aac = gst_element_factory_make ("avenc_aac", "avenc_aac");

    self->split_mux_sink = gst_element_factory_make ("splitmuxsink", "split_mux_sink");
    self->gcs_sink = gst_element_factory_make ("awss3sink", "gcs_sink");

    /* Create the empty pipeline */
    self->pipeline = gst_pipeline_new ("test-pipeline");

    if (!self->pipeline || !self->video_source || !self->video_queue || !self->video_convert || !self->x264_enc ||
    !self->audio_source || !self->audio_queue || !self->audio_convert || !self->audio_resample || !self->avenc_aac ||
    !self->split_mux_sink || !self->gcs_sink) {
        g_printerr ("Not all elements could be created.\n");
        return FALSE;
    } else {
        g_print ("All elements created successfully.\n");
    }

    /* Configure elements */
    g_object_set (self->x264_enc, "speed-preset", 1, "bitrate", 128, NULL);
    g_object_set (self->avenc_aac, "bitrate", 256, NULL);
    g_object_set (self->gcs_sink, "access-key", "add-here", "bucket", "add-here", "endpoint-uri", "https://storage.googleapis.com", "force-path-style", true, "region", "add-here", "secret-access-key", "add-here", "sync", true,  NULL);
    g_object_set(self->split_mux_sink, "max-size-time", (guint64)SEGMENT_DURATION * GST_MSECOND, "send-keyframe-requests", true, "sink", self->gcs_sink, NULL);

    // Connect the format-location signal to generate dynamic filenames
    g_signal_connect(self->split_mux_sink, "format-location", G_CALLBACK(format_location_callback), self->gcs_sink);

    g_print ("All elements configured successfully.\n");

    /* Link all elements that can be automatically linked because they have "Always" pads */
    /* Adding caps filter between video_source and video_convert */
    gst_bin_add_many (GST_BIN (self->pipeline), self->video_source, self->video_queue, self->video_convert, self->x264_enc,
    self->audio_source, self->audio_queue, self->audio_convert, self->audio_resample, self->avenc_aac,
    self->split_mux_sink, NULL);

    if (link_elements_with_video_filter (self->video_source, self->video_queue) != TRUE ||
        gst_element_link_many (self->video_queue, self->video_convert, self->x264_enc, NULL) != TRUE ||

        link_elements_with_audio_filter (self->audio_source, self->audio_queue, 48000, 2) != TRUE ||
        gst_element_link_many (self->audio_queue, self->audio_convert, self->audio_resample, NULL) != TRUE ||
        link_elements_with_audio_filter (self->audio_resample, self->avenc_aac, 16000, 1) != TRUE) {
        g_printerr ("Elements could not be linked.\n");
        gst_object_unref (self->pipeline);
        return FALSE;
    } else {
        g_print ("All elements linked successfully.\n");
    }

    /* Manually link the splitmuxsink which has "Request" pads */
    self->x264enc_src_pad = gst_element_get_static_pad (self->x264_enc, "src");
    self->splitmuxsink_video_pad = gst_element_request_pad_simple (self->split_mux_sink, "video");
    g_print ("Obtained request pad %s for splitmuxsink video branch.\n", gst_pad_get_name (self->splitmuxsink_video_pad));

    self->avenc_aac_src_pad = gst_element_get_static_pad (self->avenc_aac, "src");
    self->splitmuxsink_audio_pad = gst_element_request_pad_simple (self->split_mux_sink, "audio_%u");
    g_print ("Obtained request pad %s for splitmuxsink audio branch.\n", gst_pad_get_name (self->splitmuxsink_audio_pad));

    if (gst_pad_link (self->x264enc_src_pad, self->splitmuxsink_video_pad) != GST_PAD_LINK_OK ||
    gst_pad_link (self->avenc_aac_src_pad, self->splitmuxsink_audio_pad) != GST_PAD_LINK_OK) {
        g_printerr ("splitmuxsink could not be linked!\n");
        gst_object_unref (self->pipeline);
        return FALSE;
    } else {
        g_print ("splitmuxsink linked successfully.\n");
    }
    gst_object_unref (self->x264enc_src_pad);
    gst_object_unref (self->avenc_aac_src_pad);

    /* Start playing the pipeline */
    GstStateChangeReturn ret = gst_element_set_state (self->pipeline, GST_STATE_PLAYING);

    if (ret == GST_STATE_CHANGE_FAILURE) {
      g_error("Failed to start the pipeline\n");
    } else {
      g_print("Pipeline started successfully\n");
    }
}

// Function to check and send EOS if terminate is TRUE
void check_and_send_eos(MediaPush *self) {
    if (terminate) {
        gst_element_send_event(self->split_mux_sink, gst_event_new_eos());
        g_print("EOS event sent to the splitmuxsink\n");   
        // We can also send EOS directly to pipeline
        // gst_element_send_event(self->pipeline, gst_event_new_eos());
        // g_print("EOS event sent to the pipeline\n");       
    }
}

int main(int argc, char *argv[]) {
    MediaPush self = {0}; // Initialize the struct with zeroes
    g_mutex_init(&self.lock);

    g_mutex_lock (&self.lock);
    gboolean ret = run_splitmuxsink(&self);
    g_mutex_unlock (&self.lock);
    if(!ret){
        return ret;
    }

    signal(SIGINT, signal_handler);

    GstBus *bus = gst_element_get_bus (self.pipeline);
    GstMessage *msg;
    /* Free resources */
    while(!eos) {
        msg = gst_bus_timed_pop_filtered (bus, 1000 * GST_MSECOND, GST_MESSAGE_ERROR | GST_MESSAGE_EOS);
        if (msg) {
            if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_EOS) {
                g_print("EOS received\n");
            } else if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
                GError *err;
                gchar *debug;
                gst_message_parse_error(msg, &err, &debug);
                g_printerr("Error: %s\n", err->message);
                g_error_free(err);
                g_free(debug);
            }
            gst_message_unref(msg);
        }

        // Ensure check_and_send_eos is called one last time after the loop
        if (terminate==TRUE && eos==FALSE) {
            g_print("Sending EOS due to sigint\n");
            check_and_send_eos(&self);
            eos=TRUE;
            g_usleep(60 * G_USEC_PER_SEC); //graceful shutdown of 1 min
        }
    }

    g_print("Finished listening to bus messages\n");

    gst_element_release_request_pad (self.split_mux_sink, self.splitmuxsink_video_pad);
    gst_object_unref (self.splitmuxsink_video_pad);
    gst_element_release_request_pad (self.split_mux_sink, self.splitmuxsink_audio_pad);
    gst_object_unref (self.splitmuxsink_audio_pad);
    gst_object_unref (bus);
    gst_element_set_state (self.pipeline, GST_STATE_NULL);
    gst_object_unref (self.pipeline);

    return TRUE;
}
