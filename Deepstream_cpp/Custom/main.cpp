/*
 * Copyright (c) 2018-2020, NVIDIA CORPORATION. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "ds_header.h"
#include "nvds_box_parser.h"

#define MAX_DISPLAY_LEN 64

/* The muxer output resolution must be set if the input streams will be of
 * different resolution. The muxer will scale all the input frames to this
 * resolution. */
#define MUXER_OUTPUT_WIDTH 1920
#define MUXER_OUTPUT_HEIGHT 1080

/* Muxer batch formation timeout, for e.g. 40 millisec. Should ideally be set
 * based on the fastest source's framerate. */
#define MUXER_BATCH_TIMEOUT_USEC 40000

gint frame_number = 0;

/* pgie_src_pad_buffer_probe  will tensor and parse it. */
static GstPadProbeReturn pgie_src_pad_buffer_probe (GstPad * pad, GstPadProbeInfo * info, gpointer u_data)
{
    GstBuffer *buf = (GstBuffer *)info->data;
    NvDsBatchMeta *batch_meta = gst_buffer_get_nvds_batch_meta(buf);    NvDsObjectMeta *obj_meta = NULL;

    NvDsMetaList *l_user_meta = NULL;
    NvDsUserMeta *user_meta = NULL;
    for (l_user_meta = batch_meta->batch_user_meta_list; l_user_meta != NULL; l_user_meta = l_user_meta->next)
    {
        user_meta = (NvDsUserMeta *)(l_user_meta->data);
        if (user_meta->base_meta.meta_type == NVDS_PREPROCESS_BATCH_META)
        {
            GstNvDsPreProcessBatchMeta *preprocess_batchmeta = (GstNvDsPreProcessBatchMeta *)(user_meta->user_meta_data);
            for(auto &roi_meta: preprocess_batchmeta -> roi_vector)
            {
                NvDsMetaList *l_user = NULL;
                for (l_user = roi_meta.roi_user_meta_list; l_user != NULL; l_user = l_user->next)
                {
                    NvDsUserMeta *user_meta = (NvDsUserMeta *)(l_user->data);
                    if (user_meta->base_meta.meta_type == NVDSINFER_TENSOR_OUTPUT_META)
                    {
                        NvDsInferTensorMeta *tensor_meta = (NvDsInferTensorMeta *)(user_meta->user_meta_data);
                        float *prob = (float *)tensor_meta->out_buf_ptrs_host[0];
                        std::vector<Object> objects;
                        int img_w = 1080;
                        int img_h = 720;
                        float scale = std::min(INPUT_W / (img_w*1.0), INPUT_H / (img_h*1.0));
                        decode_outputs(prob, objects, scale, img_w, img_h);

                        for(auto& r : objects) {
                            NvDsInferParseObjectInfo oinfo;
                            
                            oinfo.classId = r.label;
                            oinfo.left    = static_cast<unsigned int>(r.rect.x);
                            oinfo.top     = static_cast<unsigned int>(r.rect.y);
                            oinfo.width   = static_cast<unsigned int>(r.rect.width);
                            oinfo.height  = static_cast<unsigned int>(r.rect.height);
                            oinfo.detectionConfidence = r.prob;
                            objectList.push_back(oinfo);
                            nvds_add_obj_meta_to_frame(frame)
                        }

                    }
                }
            }
        }
    }
    
    /* Iterate each frame metadata in batch */
    NvDsMetaList * l_frame = NULL;
    for(l_frame = batch_meta->frame_meta_list; l_frame != NULL; l_frame = l_frame->next)
    {
        NvDsFrameMeta *frame_meta = (NvDsFrameMeta *) l_frame->data;
    }
    return GST_PAD_PROBE_OK;
}

static gboolean bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
    GMainLoop *loop = (GMainLoop *) data;
    switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_EOS:
        g_print ("End of stream\n");
        g_main_loop_quit (loop);
        break;
        case GST_MESSAGE_ERROR:{
        gchar *debug;
        GError *error;
        gst_message_parse_error (msg, &error, &debug);
        g_printerr ("ERROR from element %s: %s\n",
            GST_OBJECT_NAME (msg->src), error->message);
        if (debug)
            g_printerr ("Error details: %s\n", debug);
        g_free (debug);
        g_error_free (error);
        g_main_loop_quit (loop);
        break;
        }
        default:
        break;
    }
    return TRUE;
}

int main (int argc, char *argv[])
{
    GMainLoop *loop = NULL;
    GstElement *pipeline = NULL, *source = NULL, *h264parser = NULL,
        *decoder = NULL, *streammux = NULL, *sink = NULL, *pgie = NULL, *nvvidconv = NULL,
        *nvosd = NULL;

    GstElement *transform = NULL;
    GstBus *bus = NULL;
    guint bus_watch_id;
    GstPad *osd_sink_pad = NULL;

    int current_device = -1;
    cudaGetDevice(&current_device);
    struct cudaDeviceProp prop;
    cudaGetDeviceProperties(&prop, current_device);
    /* Check input arguments */
    if (argc != 2) {
        g_printerr ("Usage: %s <H264 filename>\n", argv[0]);
        return -1;
    }

    /* Standard GStreamer initialization */
    gst_init (&argc, &argv);
    loop = g_main_loop_new (NULL, FALSE);

    /* Create gstreamer elements */
    /* Create Pipeline element that will form a connection of other elements */
    pipeline = gst_pipeline_new ("dstest1-pipeline");

    /* Source element for reading from the file */
    source = gst_element_factory_make ("filesrc", "file-source");

    /* Since the data format in the input file is elementary h264 stream,
    * we need a h264parser */
    h264parser = gst_element_factory_make ("h264parse", "h264-parser");

    /* Use nvdec_h264 for hardware accelerated decode on GPU */
    decoder = gst_element_factory_make ("nvv4l2decoder", "nvv4l2-decoder");

    /* Create nvstreammux instance to form batches from one or more sources. */
    streammux = gst_element_factory_make ("nvstreammux", "stream-muxer");

    if (!pipeline || !streammux) {
        g_printerr ("One element could not be created. Exiting.\n");
        return -1;
    }

    /* Use nvinfer to run inferencing on decoder's output,
    * behaviour of inferencing is set through config file */
    pgie = gst_element_factory_make ("nvinfer", "primary-nvinference-engine");

    /* Use convertor to convert from NV12 to RGBA as required by nvosd */
    nvvidconv = gst_element_factory_make ("nvvideoconvert", "nvvideo-converter");

    /* Create OSD to draw on the converted RGBA buffer */
    nvosd = gst_element_factory_make ("nvdsosd", "nv-onscreendisplay");

    /* Finally render the osd output */
    if(prop.integrated) {
        transform = gst_element_factory_make ("nvegltransform", "nvegl-transform");
    }
    sink = gst_element_factory_make ("nveglglessink", "nvvideo-renderer");

    if (!source || !h264parser || !decoder || !pgie
        || !nvvidconv || !nvosd || !sink) {
        g_printerr ("One element could not be created. Exiting.\n");
        return -1;
    }

    if(!transform && prop.integrated) {
        g_printerr ("One tegra element could not be created. Exiting.\n");
        return -1;
    }

    /* we set the input filename to the source element */
    g_object_set (G_OBJECT (source), "location", argv[1], NULL);

    g_object_set (G_OBJECT (streammux), "batch-size", 1, NULL);

    g_object_set (G_OBJECT (streammux), "width", MUXER_OUTPUT_WIDTH, "height",
        MUXER_OUTPUT_HEIGHT,
        "batched-push-timeout", MUXER_BATCH_TIMEOUT_USEC, NULL);

    /* Set all the necessary properties of the nvinfer element,
    * the necessary ones are : */
    g_object_set (G_OBJECT (pgie),
        "config-file-path", "dstest1_pgie_config.txt", NULL);

    /* we add a message handler */
    bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
    bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
    gst_object_unref (bus);

    /* Set up the pipeline */
    /* we add all elements into the pipeline */
    if(prop.integrated) {
        gst_bin_add_many (GST_BIN (pipeline),
            source, h264parser, decoder, streammux, pgie,
            nvvidconv, nvosd, transform, sink, NULL);
    }
    else {
    gst_bin_add_many (GST_BIN (pipeline),
        source, h264parser, decoder, streammux, pgie,
        nvvidconv, nvosd, sink, NULL);
    }

    GstPad *sinkpad, *srcpad;
    gchar pad_name_sink[16] = "sink_0";
    gchar pad_name_src[16] = "src";

    sinkpad = gst_element_get_request_pad (streammux, pad_name_sink);
    if (!sinkpad) {
        g_printerr ("Streammux request sink pad failed. Exiting.\n");
        return -1;
    }

    srcpad = gst_element_get_static_pad (decoder, pad_name_src);
    if (!srcpad) {
        g_printerr ("Decoder request src pad failed. Exiting.\n");
        return -1;
    }

    if (gst_pad_link (srcpad, sinkpad) != GST_PAD_LINK_OK) {
        g_printerr ("Failed to link decoder to stream muxer. Exiting.\n");
        return -1;
    }

    gst_object_unref (sinkpad);
    gst_object_unref (srcpad);

    /* we link the elements together */
    /* file-source -> h264-parser -> nvh264-decoder ->
    * nvinfer -> nvvidconv -> nvosd -> video-renderer */

    if (!gst_element_link_many (source, h264parser, decoder, NULL)) {
        g_printerr ("Elements could not be linked: 1. Exiting.\n");
        return -1;
    }

    if(prop.integrated) {
        if (!gst_element_link_many (streammux, pgie,
            nvvidconv, nvosd, transform, sink, NULL)) {
        g_printerr ("Elements could not be linked: 2. Exiting.\n");
        return -1;
        }
    }
    else {
        if (!gst_element_link_many (streammux, pgie,
            nvvidconv, nvosd, sink, NULL)) {
        g_printerr ("Elements could not be linked: 2. Exiting.\n");
        return -1;
        }
    }

        GstPad *pgie_srcpad=NULL;
        pgie_srcpad = gst_element_get_static_pad(pgie, "src");
        if (!pgie_srcpad)
        {
            g_print("Unable to get sink pad");
        }
        else
        {
            gst_pad_add_probe(pgie_srcpad, GST_PAD_PROBE_TYPE_BUFFER,
                pgie_src_pad_buffer_probe, NULL, NULL);
        }
        gst_object_unref(pgie_srcpad);

    /* Lets add probe to get informed of the meta data generated, we add probe to
    * the sink pad of the osd element, since by that time, the buffer would have
    * had got all the metadata. */

    /* Set the pipeline to "playing" state */
    g_print ("Now playing: %s\n", argv[1]);
    gst_element_set_state (pipeline, GST_STATE_PLAYING);

    /* Wait till pipeline encounters an error or EOS */
    g_print ("Running...\n");
    g_main_loop_run (loop);

    /* Out of the main loop, clean up nicely */
    g_print ("Returned, stopping playback\n");
    gst_element_set_state (pipeline, GST_STATE_NULL);
    g_print ("Deleting pipeline\n");
    gst_object_unref (GST_OBJECT (pipeline));
    g_source_remove (bus_watch_id);
    g_main_loop_unref (loop);
    return 0;
}
