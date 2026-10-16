# ESP32-S3 3D Model Creator

An ESP32-S3-based experimental project for capturing camera images and motion data using an MPU-6050 gyroscope/accelerometer. The collected data can be used as a foundation for creating 3D models from multiple viewpoints and sensor readings.

## Features

- ESP32-S3 camera support
- Live camera streaming through a web browser
- Save photos directly to a microSD card
- Upload, download, delete, and organize files on the SD card
- Adjustable camera resolutions
- MPU-6050 accelerometer and gyroscope support
- I2C bus scanning and sensor diagnostics
- Automatic gyroscope calibration
- Wi-Fi station mode with fallback access-point mode
- Built-in web interface requiring no additional frontend software

## Repository Contents

| File | Description |
|---|---|
| `tempad_camera_sd_webui.ino` | ESP32-S3 camera, microSD, Wi-Fi, live streaming, and web interface |
| `mpu6050_test.ino` | MPU-6050 sensor test, calibration, and serial data output |

> The current repository contains separate camera/SD and MPU-6050 test sketches. These sketches can be combined in a future version to synchronize camera captures with motion data for complete 3D reconstruction.

## Hardware Requirements

- ESP32-S3 development board with camera support
- Compatible camera module
- MPU-6050 accelerometer and gyroscope
- FAT32-formatted microSD card
- USB cable
- Optional Wi-Fi network

## Camera and microSD Wiring

The camera sketch is configured for the following ESP32-S3 pin layout:

### Camera Pins

| Signal | ESP32-S3 GPIO |
|---|---:|
| XCLK | GPIO 15 |
| SIOD/SDA | GPIO 4 |
| SIOC/SCL | GPIO 5 |
| Y9 | GPIO 16 |
| Y8 | GPIO 17 |
| Y7 | GPIO 18 |
| Y6 | GPIO 12 |
| Y5 | GPIO 10 |
| Y4 | GPIO 8 |
| Y3 | GPIO 9 |
| Y2 | GPIO 11 |
| VSYNC | GPIO 6 |
| HREF | GPIO 7 |
| PCLK | GPIO 13 |

### microSD Pins

The SD card uses 1-bit SD_MMC mode:

| Signal | ESP32-S3 GPIO |
|---|---:|
| CLK | GPIO 39 |
| CMD | GPIO 38 |
| D0 | GPIO 40 |

> Pin assignments may vary between ESP32-S3 camera boards. Check your board's schematic before connecting hardware.

## MPU-6050 Wiring

The MPU-6050 sketch uses I2C communication:

| MPU-6050 Pin | ESP32-S3 |
|---|---|
| VCC | 3.3V |
| GND | GND |
| SDA | GPIO 1 |
| SCL | GPIO 2 |
| AD0 | GND |

The default I2C address is:

```text
0x68
```

If the AD0 pin is connected to 3.3V, the sensor address changes to `0x69`. Update `MPU_ADDR` in the sketch if necessary.

## Arduino IDE Configuration

For `tempad_camera_sd_webui.ino`, use the following settings:

- **Board:** ESP32S3 Dev Module
- **USB CDC On Boot:** Enabled
- **PSRAM:** OPI PSRAM
- **Flash Size:** 16MB
- **Partition Scheme:** 16M Flash, 3MB APP, 9.9MB FATFS
- **Serial Monitor Baud Rate:** 115200

For `mpu6050_test.ino`, use:

- **Board:** ESP32S3 Dev Module
- **Serial Monitor Baud Rate:** 115200

The sketches use libraries included with the ESP32 Arduino board package. No additional libraries are required.

## Installation

1. Install the ESP32 board package in Arduino IDE.
2. Format a microSD card as FAT32.
3. Connect the camera, SD card, and optionally the MPU-6050.
4. Open the desired `.ino` file.
5. Update the hardware pin definitions if your board uses a different layout.
6. Configure your Wi-Fi credentials in `tempad_camera_sd_webui.ino`.
7. Select the correct ESP32-S3 board and port.
8. Upload the sketch.
9. Open the Serial Monitor at `115200` baud.

## Wi-Fi Configuration

Edit the `addWifiNetworks()` function before uploading:

```cpp
void addWifiNetworks() {
  wifiMultiAdd("YOUR_WIFI_NAME", "YOUR_WIFI_PASSWORD");
}
```

The board attempts to connect to the configured Wi-Fi network.

If no configured network is available, it creates its own access point:

```text
SSID: Tempad-ESP32
Password: tempad1234
```

The fallback access-point credentials can be changed using:

```cpp
#define AP_SSID "Tempad-ESP32"
#define AP_PASS "tempad1234"
```

## Using the Camera Web Interface

After startup, the Serial Monitor prints the device IP address.

Open the displayed address in a web browser:

```text
http://<ESP32-IP-ADDRESS>
```

The device may also be available at:

```text
http://tempad.local
```

The web interface provides:

- Live camera streaming
- Start and stop stream controls
- Camera resolution selection
- Photo capture
- SD card browsing
- Folder creation
- File upload
- File download
- File and folder deletion
- SD card storage information

The camera stream runs on port `81`:

```text
http://<ESP32-IP-ADDRESS>:81/stream
```

## Camera Resolutions

The web interface supports:

- QVGA — `320 × 240`
- VGA — `640 × 480`
- SVGA — `800 × 600`
- XGA — `1024 × 768`
- SXGA — `1280 × 1024`
- UXGA — `1600 × 1200`

Higher resolutions require sufficient memory and may perform better when PSRAM is enabled.

## Saving Photos

Using the **Save photo to SD** button stores captured images in:

```text
/photos/
```

Photos are automatically named using the following format:

```text
IMG_0.jpg
IMG_1.jpg
IMG_2.jpg
```

## Testing the MPU-6050

Upload `mpu6050_test.ino` to test the motion sensor.

The sketch:

1. Scans the I2C bus.
2. Checks the MPU-6050 `WHO_AM_I` register.
3. Wakes the sensor from sleep mode.
4. Configures the accelerometer and gyroscope.
5. Performs gyro calibration.
6. Continuously prints acceleration, angular velocity, and temperature readings.

Example serial output:

```text
Accel(g) X= 0.01 Y=-0.02 Z= 0.98 | Gyro(dps) X= 0.12 Y=-0.05 Z= 0.03 | Temp=25.4 C
```

Keep the MPU-6050 stationary during the calibration process.

## 3D Reconstruction Workflow

A possible future workflow for this project is:

1. Capture a sequence of images while moving the camera around an object.
2. Record MPU-6050 orientation and motion data for each capture.
3. Associate each image with its corresponding sensor timestamp.
4. Transfer the images and sensor data to a computer.
5. Process the images using a photogrammetry or 3D reconstruction pipeline.
6. Generate and export a 3D model.

The current sketches provide the camera, storage, and motion-sensor building blocks needed for this workflow. Image processing and final 3D model generation are not currently implemented on the ESP32.

## Troubleshooting

### Camera initialization failed

Check the following:

- Camera ribbon cable is fully inserted.
- Camera pin definitions match your board.
- PSRAM is set to **OPI PSRAM**.
- The correct ESP32-S3 board is selected.
- The camera module is compatible with the configured pin layout.

### SD card mount failed

Check the following:

- The card is inserted correctly.
- The card is formatted as FAT32.
- The SD card pins match the configured GPIO values.
- The card is not damaged or write-protected.
- The card capacity is supported by the board and filesystem configuration.

### No MPU-6050 device found

Check the following:

- VCC is connected to 3.3V.
- GND is connected correctly.
- SDA is connected to GPIO 1.
- SCL is connected to GPIO 2.
- AD0 is connected to GND for address `0x68`.
- The I2C lines have appropriate pull-up resistors if required.

### All MPU-6050 readings are zero

Possible causes include:

- The sensor is still in sleep mode.
- Incorrect wiring.
- Incorrect I2C address.
- A faulty or incompatible MPU-6050 module.
- Loose connections.

## Security Considerations

The web interface is intended for use on a trusted local network.

It does not include:

- User authentication
- HTTPS encryption
- Access control
- Secure file permissions

Do not expose the ESP32 web interface directly to the public internet.

## Current Limitations

- Camera and MPU-6050 functionality are currently provided as separate sketches.
- No timestamp synchronization exists between images and motion data.
- No onboard 3D reconstruction algorithm is included.
- Camera and SD pin mappings may need to be changed for different boards.
- The fallback Wi-Fi access point uses a default password unless changed.
- Large image resolutions may require PSRAM and sufficient memory.
- Deleting non-empty folders is not supported by the current web interface.

## Future Improvements

- Combine camera capture and MPU-6050 data collection into one sketch.
- Add timestamps to image filenames and sensor records.
- Store orientation data in CSV or JSON format.
- Add automatic capture intervals.
- Add motion-triggered image capture.
- Improve camera calibration.
- Add a desktop or Python reconstruction tool.
- Add authentication to the web interface.
- Add support for exporting synchronized image and sensor datasets.
- Integrate a complete photogrammetry or 3D reconstruction pipeline.

## License

No license has currently been specified for this repository. Add a license file if you plan to distribute, modify, or reuse the project publicly.
