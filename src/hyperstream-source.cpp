/*
 hyperstream-source
 Copyright (C) 2018    Will Townsend <will@townsend.io>

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation; either version 2 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License along
 with this program. If not, see <https://www.gnu.org/licenses/>
 */

#include <obs-module.h>
#include <chrono>
#include <Portal.hpp>
#include <usbmuxd.h>
#include <obs-avc.h>

#include "FFMpegVideoDecoder.h"
#include "FFMpegAudioDecoder.h"
#ifdef __APPLE__
    #include "VideoToolboxVideoDecoder.h"
#endif

#define TEXT_INPUT_NAME obs_module_text("Hyperstream.Title")

#define SETTING_DEVICE_UUID "setting_device_uuid"
#define SETTING_DEVICE_UUID_NONE_VALUE "null"
#define SETTING_PROP_LATENCY "latency"
#define SETTING_PROP_LATENCY_NORMAL 0
#define SETTING_PROP_LATENCY_LOW 1

#define SETTING_PROP_HARDWARE_DECODER "setting_use_hw_decoder"
#define SETTING_PROP_FILTER_INTENSITY "filter-intensity"

class IOSCameraInput: public portal::PortalDelegate
{
public:
    obs_source_t *source;
    obs_data_t *settings;

    bool active = false;
    obs_source_frame frame;
    std::string deviceUUID;

    std::shared_ptr<portal::Portal> sharedPortal;
    portal::Portal portal;

    VideoDecoder *videoDecoder;
#ifdef __APPLE__
    VideoToolboxDecoder videoToolboxVideoDecoder;
#endif
    FFMpegVideoDecoder ffmpegVideoDecoder;
    FFMpegAudioDecoder audioDecoder;

    // settings
    float intensity;

    IOSCameraInput(obs_source_t *source_, obs_data_t *settings)
    : source(source_), settings(settings), portal(this)
    {
        blog(LOG_INFO, "Creating instance of plugin!");

        memset(&frame, 0, sizeof(frame));

        /// In order for the internal Portal Delegates to work there
        /// must be a shared_ptr to the instance of Portal.
        ///
        /// We create a shared pointer to the heap allocated Portal
        /// instance, and wrap it up in a sharedPointer with a deleter
        /// that doesn't do anything (this is handled automatically with
        /// the class)
        auto null_deleter = [](portal::Portal *portal) { UNUSED_PARAMETER(portal); };
        auto portalReference = std::shared_ptr<portal::Portal>(&portal, null_deleter);
        sharedPortal = portalReference;

#ifdef __APPLE__
        videoToolboxVideoDecoder.source = source;
        videoToolboxVideoDecoder.Init();
#endif

        ffmpegVideoDecoder.source = source;
        ffmpegVideoDecoder.Init();

        audioDecoder.source = source;
        audioDecoder.Init();

        videoDecoder = &ffmpegVideoDecoder;

        loadSettings(settings);
        active = true;
    }

    inline ~IOSCameraInput()
    {
        auto device = portal._device;
        if (device) {
            device->disconnect();
        }

    }

    void activate() {
        blog(LOG_INFO, "Activating");
        active = true;
    }

    void deactivate() {
        blog(LOG_INFO, "Deactivating");
        active = false;
    }

    void loadSettings(obs_data_t *settings) {
        auto device_uuid = obs_data_get_string(settings, SETTING_DEVICE_UUID);

        blog(LOG_INFO, "Loaded Settings: Connecting to device");
        connectToDevice(device_uuid, false);
    }

    void reconnectToDevice()
    {
        if (deviceUUID.size() < 1) {
            return;
        }

        connectToDevice(deviceUUID, true);
    }

    void connectToDevice(std::string uuid, bool force) {

        if (portal._device) {
            // Make sure that we're not already connected to the device
            if (force == false && portal._device->uuid().compare(uuid) == 0 && portal._device->isConnected()) {
                blog(LOG_DEBUG, "Already connected to the device. Skipping.");
                return;
            } else {
                // Disconnect from from the old device
                portal._device->disconnect();
                portal._device = nullptr;
            }
        }

        blog(LOG_INFO, "Connecting to device");

        // flush the decoders 
        ffmpegVideoDecoder.Flush();
#ifdef __APPLE__
        videoToolboxVideoDecoder.Flush();
#endif

        // Find device
        auto devices = portal.getDevices();
        deviceUUID = std::string(uuid);

        int index = 0;
        std::for_each(devices.begin(), devices.end(), [this, uuid, &index](std::map<int, portal::Device::shared_ptr>::value_type &deviceMap) {
            // Add the device name to the list
            auto _uuid = deviceMap.second->uuid();

            if (_uuid.compare(uuid) == 0) {
                blog(LOG_DEBUG, "comparing \n%s\n%s\n", _uuid.c_str(), uuid.c_str());
                portal.connectToDevice(deviceMap.second);
            }

            index++;
        });
    }

    void portalDeviceDidReceivePacket(std::vector<char> packet, int type, int tag)
    {
        try
        {
            switch (type) {
                case 101: // Video Packet
                    this->videoDecoder->Input(packet, type, tag);
                    break;
                case 102: // Audio Packet
                    this->audioDecoder.Input(packet, type, tag);
                default:
                    break;
            }
        }
        catch (...)
        {
            // This isn't great, but I haven't been able to figure out what is causing
            // the exception that happens when
            //   the phone is plugged in with the app open
            //   OBS Studio is launched with the iOS Camera plugin ready
            // This also doesn't happen _all_ the time. Which makes this 'fun'..
            blog(LOG_INFO, "Exception caught...");
        }
    }

    void portalDidUpdateDeviceList(std::map<int, portal::Device::shared_ptr> deviceList)
    {
        // Update OBS Settings
        blog(LOG_INFO, "Updated device list");

        /// If there is one device in the list, then we should attempt to connect to it.
        /// I would guess that this is the main use case - one device, and it's good to
        /// attempt to automatically connect in this case, and 'just work'.
        ///
        /// If there are multiple devices, then we can't just connect to all devices.
        /// We cannot currently detect if a device is connected to another instance of the
        /// plugin, so it's not safe to attempt to connect to any devices automatically
        /// as we could be connecting to a device that is currently connected elsewhere.
        /// Due to this, if there are multiple devices, we won't do anything and will let
        /// the user configure the instance of the plugin.
        if (deviceList.size() == 1) {

            for (const auto& [index, device] : deviceList) {
                auto uuid = device.get()->uuid();

                auto isFirstTimeConnectingToDevice = deviceUUID.size() == 0;
                auto isDeviceConnected = device.get()->isConnected();
                auto isThisDeviceTheSameAsThePreviouslyConnectedDevice = deviceUUID.compare(uuid) == 0;

                if (isFirstTimeConnectingToDevice || (isThisDeviceTheSameAsThePreviouslyConnectedDevice && !isDeviceConnected)) {

                    // Set the setting so that the UI in OBS Studio is updated
                    obs_data_set_string(this->settings, SETTING_DEVICE_UUID, uuid.c_str());

                    // Connect to the device
                    connectToDevice(uuid, false);
                }
            }

        } else {
            // User will have to configure the plugin manually when more than one device is plugged in
            // due to the fact that multiple instances of the plugin can't subscribe to device events...
        }
    }
};
static IOSCameraInput *AppContext;

#pragma mark - Settings Config

static bool refresh_devices(obs_properties_t *props, obs_property_t *p, void *data)
{
    UNUSED_PARAMETER(p);

    auto cameraInput =  reinterpret_cast<IOSCameraInput*>(data);

    cameraInput->portal.reloadDeviceList();
    auto devices = cameraInput->portal.getDevices();

    obs_property_t *dev_list = obs_properties_get(props, SETTING_DEVICE_UUID);
    obs_property_list_clear(dev_list);

    obs_property_list_add_string(dev_list, "None", SETTING_DEVICE_UUID_NONE_VALUE);

    int index = 1;
    std::for_each(devices.begin(), devices.end(), [dev_list, &index](std::map<int, portal::Device::shared_ptr>::value_type &deviceMap) {
        // Add the device uuid to the list.
        // It would be neat to grab the device name somehow, but that will likely require
        // libmobiledevice instead of usbmuxd. Something to look into.
        auto uuid = deviceMap.second->uuid().c_str();
        obs_property_list_add_string(dev_list, uuid, uuid);

        // Disable the row if the device is selected as we can only
        // connect to one device to one source.
        // Disabled for now as I'm not sure how to sync status across
        // multiple instances of the plugin.
        //                      auto isConnected = deviceMap.second->isConnected();
        //                      obs_property_list_item_disable(dev_list, index, isConnected);

        index++;
    });

    return true;
}


static int sendData(int type, char* payload, int payloadSize, portal::Device& device) {
    portal::PortalFrame frame;
    frame.version = 0;
    frame.type = type;
    frame.tag = 0;

    if (!device.isConnected()) { return -1; }

    std::vector<char> packet(sizeof(portal::PortalFrame) + payloadSize);
    memcpy(packet.data(), reinterpret_cast<char*>(&frame), sizeof(portal::PortalFrame));
    if (payload && payloadSize > 0) {
        memcpy(packet.data() + sizeof(portal::PortalFrame), payload, payloadSize);
    }
    device.send(packet);
}


const int PREV_FILTER_PACKET_TYPE = 104;
static bool prev_filter(obs_properties_t*, obs_property_t*, void *data) {
    blog(LOG_INFO, "prev filter");
    auto device = AppContext->portal._device;
    if (device) {
        sendData(PREV_FILTER_PACKET_TYPE, NULL, 0, *device);
    }
    return true;
}

const int NEXT_FILTER_PACKET_TYPE = 105;
static bool next_filter(obs_properties_t*, obs_property_t*, void *data) {
    blog(LOG_INFO, "next filter");
    auto device = AppContext->portal._device;
    if (device) {
        sendData(NEXT_FILTER_PACKET_TYPE, NULL, 0, *device);
    }
    return true;
}

const int WILDCARD_PACKET_TYPE = 107;
static bool wildcard(obs_properties_t*, obs_property_t*, void *data) {
    blog(LOG_INFO, "wildcard");
    auto cameraInput = reinterpret_cast<IOSCameraInput* >(data);
    auto device = cameraInput->portal._device;
    if (!device) { return false; }
    sendData(WILDCARD_PACKET_TYPE, NULL, 0, *device);
    return true;
}

static bool update_device(obs_properties_t*, obs_property_t*, obs_data *settings) {
    auto uuid = obs_data_get_string(settings, SETTING_DEVICE_UUID);
    blog(LOG_INFO, "device value: %s", uuid);
    AppContext->connectToDevice(uuid, false);
    // obs_source_output_video(AppContext->source, NULL);
    return true;
    // return AppContext->portal._device != NULL;
}

static bool update_latency(obs_properties_t*, obs_property_t*, obs_data *settings) {
    const bool is_unbuffered =
        (obs_data_get_int(settings, SETTING_PROP_LATENCY) == SETTING_PROP_LATENCY_LOW);
    obs_source_set_async_unbuffered(AppContext->source, is_unbuffered);
    blog(LOG_INFO, "latency value: %d", is_unbuffered);
    return true;
}

#ifdef __APPLE__
static bool update_hardware_decoding(obs_properties_t*, obs_property_t*, obs_data *settings) {
    bool useHardwareDecoder = obs_data_get_bool(settings, SETTING_PROP_HARDWARE_DECODER);
    if (useHardwareDecoder) {
        AppContext->videoDecoder = &AppContext->videoToolboxVideoDecoder;
    } else {
        AppContext->videoDecoder = &AppContext->ffmpegVideoDecoder;
    }
    blog(LOG_INFO, "hardware decoding value: %d", useHardwareDecoder);
    return true;
}
#endif

static bool reconnect_to_device(obs_properties_t*, obs_property_t*, void *data)
{
    auto cameraInput =  reinterpret_cast<IOSCameraInput* >(data);
    cameraInput->reconnectToDevice();
    return false;
}

#pragma mark - Plugin Callbacks

static const char *GetIOSCameraInputName(void *)
{
    return TEXT_INPUT_NAME;
}

static void *CreateIOSCameraInput(obs_data_t *settings, obs_source_t *source)
{
    IOSCameraInput *cameraInput = nullptr;
    try
    {
        cameraInput = new IOSCameraInput(source, settings);
        AppContext = cameraInput;
    }
    catch (const char *error)
    {
        blog(LOG_ERROR, "Could not create device '%s': %s", obs_source_get_name(source), error);
    }
    return cameraInput;
}

static void DestroyIOSCameraInput(void *data)
{
    delete reinterpret_cast<IOSCameraInput *>(data);
}

static void DeactivateIOSCameraInput(void *data)
{
    if (AppContext) {
        auto device = AppContext->portal._device;
        if (device) {
            // device->disconnect();
        }
    }

    auto cameraInput =  reinterpret_cast<IOSCameraInput*>(data);
    cameraInput->deactivate();
}

static void ActivateIOSCameraInput(void *data)
{
    auto cameraInput = reinterpret_cast<IOSCameraInput*>(data);
    cameraInput->activate();
}

static obs_properties_t *GetIOSCameraProperties(void *data)
{
    UNUSED_PARAMETER(data);
    obs_properties_t *ppts = obs_properties_create();

    obs_properties_add_button(ppts, "setting_refresh_devices", "Refresh Devices", refresh_devices);
    obs_properties_add_button(ppts, "setting_button_connect_to_device", "Connect to Device", reconnect_to_device);

    obs_property_t *dev_list = obs_properties_add_list(ppts, SETTING_DEVICE_UUID,
                                                       "iOS Device",
                                                       OBS_COMBO_TYPE_LIST,
                                                       OBS_COMBO_FORMAT_STRING);
    obs_property_set_modified_callback(dev_list, update_device);

    obs_property_list_add_string(dev_list, "", "");

    refresh_devices(ppts, dev_list, data);

    obs_properties_add_button(ppts, "setting_prev_filter", "Prev Filter", prev_filter);
    obs_properties_add_button(ppts, "setting_next_filter", "Next Filter", next_filter);
    obs_properties_add_button(ppts, "setting_wildcard", "Wildcard", wildcard);

    obs_property_t* filter = obs_properties_add_float_slider(ppts, SETTING_PROP_FILTER_INTENSITY, "Intensity", 0.0, 1.0, 0.01);
    // obs_property_set_modified_callback(filter, update_filter);

    obs_property_t* latency_modes = obs_properties_add_list(ppts, SETTING_PROP_LATENCY, obs_module_text("Hyperstream.Settings.Latency"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
    obs_property_list_add_int(latency_modes,
        obs_module_text("Hyperstream.Settings.Latency.Normal"),
        SETTING_PROP_LATENCY_NORMAL);
    obs_property_list_add_int(latency_modes,
        obs_module_text("Hyperstream.Settings.Latency.Low"),
        SETTING_PROP_LATENCY_LOW);
    obs_property_set_modified_callback(latency_modes, update_latency);

#ifdef __APPLE__
    obs_property_t* hardware_decoding = obs_properties_add_bool(ppts, SETTING_PROP_HARDWARE_DECODER,
        obs_module_text("Hyperstream.Settings.UseHardwareDecoder"));
    obs_property_set_modified_callback(hardware_decoding, update_hardware_decoding);
#endif

    return ppts;
}


static void GetIOSCameraDefaults(obs_data_t *settings)
{
    obs_data_set_default_string(settings, SETTING_DEVICE_UUID, "");
    obs_data_set_default_int(settings, SETTING_PROP_LATENCY, SETTING_PROP_LATENCY_LOW);
#ifdef __APPLE__
    obs_data_set_default_bool(settings, SETTING_PROP_HARDWARE_DECODER, false);
#endif
}

static void SaveIOSCameraInput(void *data, obs_data_t *settings)
{
    UNUSED_PARAMETER(data);
    UNUSED_PARAMETER(settings);
}

static void UpdateIOSCameraInput(void *data, obs_data_t *settings) {
    if (!AppContext) { return; }
    float intensity = (float)obs_data_get_double(settings, SETTING_PROP_FILTER_INTENSITY);
    if (AppContext->intensity != intensity) {
        AppContext->intensity = intensity;

        auto device = AppContext->portal._device;
        if (device) {
            char* payload = reinterpret_cast<char*>(&intensity);
            sendData(106, payload, sizeof(float), *device);
        }
    }
}

void RegisterIOSCameraSource()
{
    obs_source_info info = {};
    info.id              = "ios-camera-source";
    info.type            = OBS_SOURCE_TYPE_INPUT;
    info.output_flags    = OBS_SOURCE_ASYNC_VIDEO | OBS_SOURCE_AUDIO;
    info.get_name        = GetIOSCameraInputName;

    info.create          = CreateIOSCameraInput;
    info.destroy         = DestroyIOSCameraInput;

    info.deactivate      = DeactivateIOSCameraInput;
    info.activate        = ActivateIOSCameraInput;

    info.get_defaults    = GetIOSCameraDefaults;
    info.get_properties  = GetIOSCameraProperties;
    info.save            = SaveIOSCameraInput;
    info.update          = UpdateIOSCameraInput;
    obs_register_source(&info);
}
