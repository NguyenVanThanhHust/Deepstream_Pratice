#ifndef __DEEPSTREAM_ACTION_H__
#define __DEEPSTREAM_ACTION_H__

#include <cuda_runtime_api.h>

#include <fstream>
#include <functional>
#include <gst/gst.h>
#include <glib.h>
#include <inttypes.h>
#include <iostream>
#include <math.h>
#include <memory>
#include <queue>
#include <sys/time.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <unordered_map>
#include <vector>

#include "gstnvdsmeta.h"
#include "nvdspreprocess_meta.h"
#include "gstnvdsinfer.h"

#ifndef PLATFORM_TEGRA
#include "gst-nvmessage.h"
#endif


#endif