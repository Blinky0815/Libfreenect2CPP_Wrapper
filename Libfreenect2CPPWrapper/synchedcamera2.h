/* 
 *    Copyright [2015] Olaf - blinky0815 - christ ]
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 * 
 *      http://www.apache.org/licenses/LICENSE-2.0
 * 
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/* 
 * File:   SynchedCamera2.h
 * Author: Blinky0815
 *
 * Created on November 19, 2015, 3:41 PM
 * Updated on July, 19, 2018, 4:59 PM
 */

#ifndef SYNCHEDCAMERA2_H
#define SYNCHEDCAMERA2_H
#include <GLFW/glfw3.h>
#include <iostream>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <float.h>
#include <math.h>
#include <algorithm>
#include <libfreenect2/libfreenect2.hpp>
#include <libfreenect2/frame_listener_impl.h>
#include <libfreenect2/registration.h>
#include <libfreenect2/packet_pipeline.h>
#include <libfreenect2/logger.h>
#include <signal.h>
#include <chrono>

using namespace std;

bool camera_shutdown = false; ///< Whether the running application should shut down.

static void sigint_handler(int s) {
    camera_shutdown = true;
}

enum PIPELINE {
    GL, CL, CPU, CUDA, CUDA_KDE, CL_KDE
};

class SynchedCamera2 {
public:

    SynchedCamera2() {
    }

    virtual ~SynchedCamera2() {
        cout << "SynchedCamera2" << endl;
    }

    void getSynchedFrames() {
        listener->waitForNewFrame(frames, 10 * 1000);
        rgbFrame = frames[libfreenect2::Frame::Color];
        irFrame = frames[libfreenect2::Frame::Ir];
        depthFrame = frames[libfreenect2::Frame::Depth];
        registration->apply(rgbFrame, depthFrame, undistortedFrame, registeredFrame);

        this->depthVideoTimeStamp = depthFrame->timestamp;
        this->rgbVideoTimeStamp = rgbFrame->timestamp;
        this->irVideoTimeStamp = irFrame->timestamp;

        copy(rgbFrame->data, rgbFrame->data + rgbData.size(), rgbData.begin());
        copy(irFrame->data, irFrame->data + irData.size(), irData.begin());
        memcpy(depthData.data(), depthFrame->data, depthData.size() * sizeof (float_t));

        copy(undistortedFrame->data, undistortedFrame->data + undistortedData.size(), undistortedData.begin());
        copy(registeredFrame->data, registeredFrame->data + registeredData.size(), registeredData.begin());
        listener->release(frames);
    }

    void init(float_t minDepth = 0.3f, float_t maxDepth = 10.0f, bool useBilateralFilter = true, bool useEdgeAwareFilter = true, int gpu_id = 0, PIPELINE pipelineType = CL) {
        libfreenect2::setGlobalLogger(libfreenect2::createConsoleLogger(libfreenect2::Logger::Debug));
        Libfreenect2FileLogger filelogger(getenv("LOGFILE"));
        if (filelogger.good())
            libfreenect2::setGlobalLogger(&filelogger);

        if (freenect2.enumerateDevices() == 0) {
            std::cout << "no device connected!" << std::endl;
        }
        std::string serial = freenect2.getDefaultDeviceSerialNumber();
        cout << "serial number: " << serial << endl;

        if (pipelineType == CPU) {
            pipeline = new libfreenect2::CpuPacketPipeline();
        } else if (pipelineType == GL) {
            pipeline = new libfreenect2::OpenGLPacketPipeline();
        } else if (pipelineType == CL) {
            pipeline = new libfreenect2::OpenCLPacketPipeline(gpu_id);
        } else if (pipelineType == CL_KDE) {
            pipeline = new libfreenect2::OpenCLKdePacketPipeline(gpu_id);
        }
#ifdef LIBFREENECT2_WITH_CUDA_SUPPORT
        else if (pipelineType == CUDA) {
            pipeline = new libfreenect2::CudaPacketPipeline(gpu_id);
        } else if (pipelineType == CUDA_KDE) {
            pipeline = new libfreenect2::CudaKdePacketPipeline(gpu_id);
        }
#endif
        if (pipeline) {
            dev = freenect2.openDevice(serial, pipeline);
        }
        if (dev == nullptr) {
            cout << "OpenGLPacketPipeline()" << endl;
            cerr << "could not open device " << endl;
        } else {

            signal(SIGINT, sigint_handler);
            camera_shutdown = false;
        }

        signal(SIGINT, sigint_handler);
        camera_shutdown = false;

        listener = new libfreenect2::SyncMultiFrameListener(libfreenect2::Frame::Color | libfreenect2::Frame::Ir | libfreenect2::Frame::Depth);
        undistortedFrame = new libfreenect2::Frame(512, 424, 4);
        registeredFrame = new libfreenect2::Frame(512, 424, 4);
    }

    void start() {

        dev->setColorFrameListener(listener);
        dev->setIrAndDepthFrameListener(listener);
        dev->start();

        std::cout << "device serial: " << dev->getSerialNumber() << std::endl;
        std::cout << "device firmware: " << dev->getFirmwareVersion() << std::endl;
        registration = new libfreenect2::Registration(dev->getIrCameraParams(), dev->getColorCameraParams());
        listener->waitForNewFrame(frames, 10 * 1000);
        rgbFrame = frames[libfreenect2::Frame::Color];
        irFrame = frames[libfreenect2::Frame::Ir];
        depthFrame = frames[libfreenect2::Frame::Depth];
        registration->apply(rgbFrame, depthFrame, undistortedFrame, registeredFrame);
        rgbWidth = rgbFrame->width;
        rgbHeight = rgbFrame->height;

        irWidth = irFrame->width;
        irHeight = irFrame->height;

        depthWidth = depthFrame->width;
        depthHeight = depthFrame->height;

        registeredHeight = registeredFrame->height;
        registeredWidth = registeredFrame->width;

        undistortedHeight = undistortedFrame->height;
        undistortedWidth = undistortedFrame->width;

        rgbData.resize(getRGBframeWidth() * getRGBframeHeight() * rgbFrame->bytes_per_pixel);
        depthData.resize(getDepthFrameWidth() * getDepthFrameHeight());
        irData.resize(getIRframeWidth() * getIRframeHeight() * irFrame->bytes_per_pixel);
        registeredData.resize(getRegisteredFrameWidth() * getRegisteredFrameHeight() * registeredFrame->bytes_per_pixel);
        undistortedData.resize(getUndistortedFrameWidth() * getUndistortedFrameHeight() * undistortedFrame->bytes_per_pixel);
        listener->release(frames);
    }

    void mirrorHorizontally(vector<float_t>& depthmap) {
        int j = 0;
        for (int i = 0; i < getDepthFrameHeight(); i++, j += getDepthFrameWidth()) {
            std::reverse(depthData.begin() + j, depthData.begin() + j + getDepthFrameWidth());
        }
    }


    //the Kinect V2's frame size is smaller than the Kinect V1's frame size.

    void toKinecV1(vector<float_t>& kinect_V2_depthmap, vector<float_t>& kinect_V1_depthmap, vector<uint8_t>& kinect_V2_colormap, vector<uint8_t>& kinect_V1_colormap) {
        const int width = getRegisteredFrameWidth();
        const int height = getRegisteredFrameHeight();
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int idx_kinec1 = (y + 28) * 640 + x + 64;
                const int idx_kinec2 = y * width + x;
                kinect_V1_depthmap[idx_kinec1] = kinect_V2_depthmap[idx_kinec2];
                kinect_V1_colormap[4 * idx_kinec1] = kinect_V2_colormap[4 * idx_kinec2];
                kinect_V1_colormap[4 * idx_kinec1 + 1] = kinect_V2_colormap[4 * idx_kinec2 + 1];
                kinect_V1_colormap[4 * idx_kinec1 + 2] = kinect_V2_colormap[4 * idx_kinec2 + 2];
                kinect_V1_colormap[4 * idx_kinec1 + 3] = kinect_V2_colormap[4 * idx_kinec2 + 3];
            }
        }
    }

    const int getRGBframeWidth() {

        return rgbWidth;
    }

    const int getRGBframeHeight() {

        return rgbHeight;
    }

    const int getIRframeWidth() {

        return irWidth;
    }

    const int getIRframeHeight() {

        return irHeight;
    }

    const int getDepthFrameWidth() {

        return depthWidth;
    }

    const int getDepthFrameHeight() {

        return depthHeight;
    }

    const int getRegisteredFrameWidth() {

        return registeredWidth;
    }

    const int getRegisteredFrameHeight() {

        return registeredHeight;
    }

    const int getUndistortedFrameWidth() {

        return undistortedWidth;
    }

    const int getUndistortedFrameHeight() {

        return undistortedHeight;
    }

    const int getRGBvideoTimeStamp() {

        return rgbVideoTimeStamp;
    }

    const int getDepthVideoTimeStamp() {

        return depthVideoTimeStamp;
    }

    const int getIRvideoTimeStamp() {

        return irVideoTimeStamp;
    }

    const string getSerialNumber() {

        return dev->getSerialNumber();
    }

    const int getFirmwareVersion() {

        dev->getFirmwareVersion();
    }

    void shutDown() {
        if (listener != NULL) {
            listener->release(frames);
        }
        dev->stop();
        dev->close();
        if (listener != NULL) {
            delete listener;
        }
        if (registration != NULL) {
            delete registration;
        }
    }

    void selftest() {

        init();
        start();
        cout << "RGB width: " << getRGBframeWidth() << endl;
        cout << "RGB height: " << getRGBframeHeight() << endl;
        cout << "IR width: " << getIRframeWidth() << endl;
        cout << "IR height: " << getIRframeHeight() << endl;
        cout << "depth width: " << getDepthFrameWidth() << endl;
        cout << "depth height: " << getDepthFrameHeight() << endl;

        cout << "getting 30 frames" << endl;
        for (int i = 0; i < 30; i++) {
            cout << "Frame: " << i << endl;
            getSynchedFrames();
        }
        cout << "rgb video image stamp: " << getRGBvideoTimeStamp() << endl;
        cout << "depth video image stamp: " << getDepthVideoTimeStamp() << endl;
        cout << "ir video image stamp: " << getIRvideoTimeStamp() << endl;

        shutDown();
    }

    vector<uint8_t>& getRGB_Data() {

        return this->rgbData;
    }

    vector<float_t>& getDepthData() {

        return this->depthData;
    }

    vector<uint8_t>& geIR_Data() {

        return this->irData;
    }

    vector<uint8_t>& getRegisteredData() {

        return this->registeredData;
    }

    vector<uint8_t>& getUndistortedData() {

        return this->undistortedData;
    }

private:
    libfreenect2::FrameMap frames;
    libfreenect2::Freenect2 freenect2;
    libfreenect2::SyncMultiFrameListener* listener = nullptr;
    libfreenect2::PacketPipeline *pipeline = nullptr;
    libfreenect2::Frame* registeredFrame = nullptr;
    libfreenect2::Frame* undistortedFrame = nullptr;
    libfreenect2::Registration* registration = nullptr;
    libfreenect2::Freenect2Device *dev = nullptr;
    libfreenect2::Frame *rgbFrame = nullptr;
    libfreenect2::Frame *irFrame = nullptr;
    libfreenect2::Frame *depthFrame = nullptr;
    int rgbWidth;
    int rgbHeight;
    int irWidth;
    int irHeight;
    int depthWidth;
    int depthHeight;
    int registeredWidth;
    int registeredHeight;
    int undistortedWidth;
    int undistortedHeight;
    uint32_t rgbVideoTimeStamp;
    uint32_t depthVideoTimeStamp;
    uint32_t irVideoTimeStamp;

    vector<uint8_t> rgbData;
    vector<float_t> depthData;
    vector<uint8_t> irData;
    vector<uint8_t> registeredData;
    vector<uint8_t> undistortedData;

    class Libfreenect2FileLogger : public libfreenect2::Logger {
    private:

        std::ofstream logfile_;
    public:

        Libfreenect2FileLogger(const char *filename) {
            if (filename)
                logfile_.open(filename);
            level_ = Debug;
        }

        bool good() {
            return logfile_.is_open() && logfile_.good();
        }

        virtual void log(Level level, const std::string &message) {
            logfile_ << "[" << libfreenect2::Logger::level2str(level) << "] " << message << std::endl;
        }
    };
};

#endif /* SYNCHEDCAMERA2_H */

