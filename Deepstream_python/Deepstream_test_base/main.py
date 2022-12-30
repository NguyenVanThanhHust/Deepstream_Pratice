import sys
sys.path.append('../')
import platform
import configparser

sys.path.append('/opt/deepstream_python_apps/bindings/build/')
import gi
gi.require_version('Gst', '1.0')
from gi.repository import GLib, Gst
from common.is_aarch_64 import is_aarch64
from common.bus_call import bus_call
from loguru import logger

import pyds

PGIE_CLASS_ID_VEHICLE = 0
PGIE_CLASS_ID_BICYCLE = 1
PGIE_CLASS_ID_PERSON = 2
PGIE_CLASS_ID_ROADSIGN = 3
past_tracking_meta=[0]

def osd_sink_pad_buffer_probe(pad, info, u_data):
    frame_number = 0
    obj_counter = {
        PGIE_CLASS_ID_VEHICLE: 0,
        PGIE_CLASS_ID_BICYCLE: 1, 
        PGIE_CLASS_ID_PERSON: 2,
        PGIE_CLASS_ID_ROADSIGN: 3
    }

    num_rects = 0
    gst_buffer = info.get_buffer()
    if not gst_buffer:
        print("Unable to to get GstBuffer")
        return 
    
    # Retrieve batch meta from gst_buffer
    batch_meta = pyds.gst_buffer_get_nvds_batch_meta(hash(gst_buffer))
    list_frame = batch_meta.frame_meta_list
    while list_frame is not None:
        try:
            frame_meta = pyds.NvDsObjectMeta.cast(list_frame.data)
        except StopIteration:
            break
    
        frame_name = frame_meta.frame_num
        num_rects = frame_meta.num_obj_meta
        list_obj = frame_meta.obj_meta_list
        while list_obj is not None:
            try:
                obj_meta = pyds.NvDsObjectMeta.cast(list_obj.data)
            except StopIteration:
                break
            obj_counter[obj_meta.class_id] += 1
            try:
                list_obj = list_obj.next
            except StopIteration:
                break

        # Acquiring a display meta object. The memory ownership remains in
        # the C code so downstream plugins can still access it. Otherwise
        # the garbage collector will claim it when this probe function exits.
        display_meta=pyds.nvds_acquire_display_meta_from_pool(batch_meta)
        display_meta.num_labels = 1
        py_nvosd_text_params = display_meta.text_params[0]
        # Setting display text to be shown on screen
        # Note that the pyds module allocates a buffer for the string, and the
        # memory will not be claimed by the garbage collector.
        # Reading the display_text field here will return the C address of the
        # allocated string. Use pyds.get_string() to get the string content.
        py_nvosd_text_params.display_text = "Frame Number={} Number of Objects={} Vehicle_count={} Person_count={}".format(frame_number, num_rects, obj_counter[PGIE_CLASS_ID_VEHICLE], obj_counter[PGIE_CLASS_ID_PERSON])

        # Now set the offsets where the string should appear
        py_nvosd_text_params.x_offset = 10
        py_nvosd_text_params.y_offset = 12

        # Font , font-color and font-size
        py_nvosd_text_params.font_params.font_name = "Serif"
        py_nvosd_text_params.font_params.font_size = 10
        # set(red, green, blue, alpha); set to White
        py_nvosd_text_params.font_params.font_color.set(1.0, 1.0, 1.0, 1.0)

        # Text background color
        py_nvosd_text_params.set_bg_clr = 1
        # set(red, green, blue, alpha); set to Black
        py_nvosd_text_params.text_bg_clr.set(0.0, 0.0, 0.0, 1.0)
        # Using pyds.get_string() to get display_text as string
        print(pyds.get_string(py_nvosd_text_params.display_text))
        pyds.nvds_add_display_meta_to_frame(frame_meta, display_meta)
        try:
            l_frame=l_frame.next
        except StopIteration:
            break
    #past traking meta data
    if(past_tracking_meta[0]==1):
        l_user=batch_meta.batch_user_meta_list
        while l_user is not None:
            try:
                # Note that l_user.data needs a cast to pyds.NvDsUserMeta
                # The casting is done by pyds.NvDsUserMeta.cast()
                # The casting also keeps ownership of the underlying memory
                # in the C code, so the Python garbage collector will leave
                # it alone
                user_meta=pyds.NvDsUserMeta.cast(l_user.data)
            except StopIteration:
                break
            if(user_meta and user_meta.base_meta.meta_type==pyds.NvDsMetaType.NVDS_TRACKER_PAST_FRAME_META):
                try:
                    # Note that user_meta.user_meta_data needs a cast to pyds.NvDsPastFrameObjBatch
                    # The casting is done by pyds.NvDsPastFrameObjBatch.cast()
                    # The casting also keeps ownership of the underlying memory
                    # in the C code, so the Python garbage collector will leave
                    # it alone
                    pPastFrameObjBatch = pyds.NvDsPastFrameObjBatch.cast(user_meta.user_meta_data)
                except StopIteration:
                    break
                for trackobj in pyds.NvDsPastFrameObjBatch.list(pPastFrameObjBatch):
                    print("streamId=",trackobj.streamID)
                    print("surfaceStreamID=",trackobj.surfaceStreamID)
                    for pastframeobj in pyds.NvDsPastFrameObjStream.list(trackobj):
                        print("numobj=",pastframeobj.numObj)
                        print("uniqueId=",pastframeobj.uniqueId)
                        print("classId=",pastframeobj.classId)
                        print("objLabel=",pastframeobj.objLabel)
                        for objlist in pyds.NvDsPastFrameObjList.list(pastframeobj):
                            print('frameNum:', objlist.frameNum)
                            print('tBbox.left:', objlist.tBbox.left)
                            print('tBbox.width:', objlist.tBbox.width)
                            print('tBbox.top:', objlist.tBbox.top)
                            print('tBbox.right:', objlist.tBbox.height)
                            print('confidence:', objlist.confidence)
                            print('age:', objlist.age)
            try:
                l_user=l_user.next
            except StopIteration:
                break
    return Gst.PadProbeReturn.OK

def make_element(type_element, name_element=None):
    logger.info("Create: {}".format(name_element))
    element = Gst.ElementFactory.make(type_element, name_element)
    if not element:
        logger.error("Can't create {}".format(name_element))
        sys.exit(1)
    return element

def main(args):
    # Check number of argument
    if(len(args) < 2):
        print("Useage {} <h264 stream> ".format(args[0]))
        return 
    
    # Initialize Gst streamer
    if len(args) == 3:
        past_tracking_meta[0] = 3
    Gst.init(None)

    # Create Pipelilne element to from connection of other elements
    logger.info("Create Pipeline")
    pipeline = Gst.Pipeline()
    if not pipeline:
        logger.error("Can't create pipelline")
        sys.exit()
    
    # Source element to read from file
    source = make_element("filesrc", "file-source")

    # h264 Parser for to parse input file
    h264_parser = make_element("h264parse", "h264-parser")

    # nvdec h264 to decode h264 stream
    decoder = make_element("nvv4l2decoder", "nv4l2-decoder")

    # Streammux to form batches from 1 or >2 sources
    streammux = make_element("nvstreammux", "stream-muxer")

    # Primary gpu infer engine to use model
    pgie = make_element("nvinfer", "primary-inference")

    # tracker to track detected objects
    tracker = make_element("nvtracker", "tracker")

    # Secondary infer engine for classifier
    sgie1 = make_element("nvinfer", "secondary1-infer-engine")

    sgie2 = make_element("nvinfer", "secondary2-infer-engine")

    sgie3 = make_element("nvinfer", "secondary3-infer-engine")

    # Convert to convert video
    nvvid_convertor = make_element("nvvideoconvert", "convertor")

    # Create OSD to draw RGBA bufffer
    nvosd = make_element("nvdsosd", "onscreendisplay")

    if is_aarch64():
        logger.info("This is on arrch64")
        transform = Gst.ElementFactory.make("nvegltransform", "nvegl-transform")

    sink = make_element("nveglglessink", "nvvideo-renderer")

    logger.info("Playing file: {}".format(args[1]))

    logger.info("Setting para")
    source.set_property('location', args[1])
    streammux.set_property('height', 1920)
    streammux.set_property('width', 1080)
    streammux.set_property('batch-size', 1)
    streammux.set_property('batched-push-timeout', 4000000)

    logger.info("Set properties for infer engine")
    pgie.set_property("config-file-path", "pgie_config.txt")
    sgie1.set_property("config-file-path", "sgie1_config.txt")
    sgie2.set_property("config-file-path", "sgie2_config.txt")
    sgie3.set_property("config-file-path", "sgie3_config.txt")

    logger.info("Set properties for tracker")
    config = configparser.ConfigParser()
    config.read("tracker_config.txt")
    config.sections()

    for key in config['tracker']:
        if key == 'tracker-width' :
            tracker_width = config.getint('tracker', key)
            tracker.set_property('tracker-width', tracker_width)
        if key == 'tracker-height' :
            tracker_height = config.getint('tracker', key)
            tracker.set_property('tracker-height', tracker_height)
        if key == 'gpu-id' :
            tracker_gpu_id = config.getint('tracker', key)
            tracker.set_property('gpu_id', tracker_gpu_id)
        if key == 'll-lib-file' :
            tracker_ll_lib_file = config.get('tracker', key)
            tracker.set_property('ll-lib-file', tracker_ll_lib_file)
        if key == 'll-config-file' :
            tracker_ll_config_file = config.get('tracker', key)
            tracker.set_property('ll-config-file', tracker_ll_config_file)
        if key == 'enable-batch-process' :
            tracker_enable_batch_process = config.getint('tracker', key)
            tracker.set_property('enable_batch_process', tracker_enable_batch_process)
        if key == 'enable-past-frame' :
            tracker_enable_past_frame = config.getint('tracker', key)
            tracker.set_property('enable_past_frame', tracker_enable_past_frame)

    logger.info("Adding element to Pipeline")
    pipeline.add(source)
    pipeline.add(h264_parser)
    pipeline.add(decoder)
    pipeline.add(streammux)
    pipeline.add(pgie)
    pipeline.add(tracker)
    pipeline.add(sgie1)
    pipeline.add(sgie2)
    pipeline.add(sgie3)
    pipeline.add(nvvid_convertor)
    pipeline.add(nvosd)
    pipeline.add(sink)
    if is_aarch64():
        pipeline.add(transform)

    # we link the elements together
    # file-source -> h264-parser -> nvh264-decoder ->
    # nvinfer -> nvvidconv -> nvosd -> video-renderer
    print("Linking elements in the Pipeline \n")
    source.link(h264_parser)
    h264_parser.link(decoder)

    sinkpad = streammux.get_request_pad("sink_0")
    if not sinkpad:
        logger.error("Unable to get the sink pad of streammux")
        sys.exit()
    
    srcpad = decoder.get_static_pad("src")
    if not srcpad:
        logger.error("Unable to get the source pad of decoder")
        sys.exit()

    srcpad.link(sinkpad)
    streammux.link(pgie)
    pgie.link(tracker)
    tracker.link(sgie1)
    sgie1.link(sgie2)
    sgie2.link(sgie3)
    sgie3.link(nvvid_convertor)
    nvvid_convertor.link(nvosd)
    if is_aarch64():
        nvosd.link(transform)
        transform.link(sink)
    else:
        nvosd.link(sink)

    logger.info("Create an event loop and feed gstreamer bus message to it")
    loop = GLib.MainLoop()

    bus = pipeline.get_bus()
    bus.add_signal_watch()
    bus.connect("message", bus_call, loop)

    osdsinkpad = nvosd.get_static_pad("sink")
    if not osdsinkpad:
        logger.error(" Unable to get sink pad of nvosd \n")
        sys.exit()
    osdsinkpad.add_probe(Gst.PadProbeType.BUFFER, osd_sink_pad_buffer_probe, 0)

    logger.info("Starting pipeline")
    pipeline.set_state(Gst.State.PLAYING)
    try:
        loop()
    except:
        pass

    pipeline.set_state(Gst.State.NULL)

if __name__ == "__main__":
    sys.exit(main(sys.argv))