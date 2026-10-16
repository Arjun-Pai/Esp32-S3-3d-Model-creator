# ESP32-S3 3D Model Creator

An experimental ESP32-S3 project for creating 3D models from camera images using a rotating turntable. The project includes camera capture, optional MPU-6050 motion sensing, browser-based silhouette processing, visual-hull reconstruction, and binary STL export.

The main scanner workflow runs directly in a web browser. The ESP32-S3 provides camera frames and controls the optional turntable motor, while the browser performs the image processing and generates the 3D model.

## Features

- ESP32-S3 camera support
- Browser-based camera control interface
- Live camera preview
- Background capture and object silhouette extraction
- Automatic multi-angle image capture
- Manual rotation mode for turntables without a motor
- Optional stepper motor control using an A4988 or DRV8825 driver
- Visual-hull voxel reconstruction in the browser
- Surface-nets mesh generation
- Interactive browser preview of the generated model
- Binary STL file download
- Adjustable camera resolution and JPEG quality
- Optional microSD card photo storage and file management
- Optional MPU-6050 accelerometer and gyroscope support
- Gyroscope calibration and sensor diagnostics
- Wi-Fi station mode with fallback access-point mode
- No external web server or frontend software required

## Repository Contents

| File | Description |
|---|---|
| `3d_scanner.ino` | Main browser-based 3D scanner with camera capture, turntable control, silhouette processing, visual-hull reconstruction, and STL export |
| `tempad_camera_sd_webui.ino` | Camera streaming, microSD file manager, photo capture, file upload/download/delete, and Wi-Fi web interface |
| `mpu6050_test.ino` | Standalone MPU-6050 diagnostic sketch with I2C scanning, initialization, calibration, and sensor output |

## How the 3D Scanner Works

The `3d_scanner.ino` sketch uses the following workflow:

1. Connect the ESP32-S3 to Wi-Fi.
2. Open the scanner web interface.
3. Place an object on a turntable.
4. Capture an empty background without the object.
5. Adjust the camera and mask threshold.
6. Capture images from multiple angles.
7. Rotate the turntable automatically or manually.
8. Process the silhouettes in the browser.
9. Generate a visual-hull voxel model.
10. Convert the model into a surface mesh.
11. Preview the model in the browser.
12. Download the result as a binary STL file.

The ESP32-S3 does not perform the reconstruction calculations. Image processing and mesh generation run in the browser on a phone, tablet, or computer.

## Hardware Requirements

### Required for the 3D scanner

- ESP32-S3 development board with camera support
- Compatible camera module, such as OV3660
- Computer, phone, or tablet with a modern web browser
- Object turntable
- Suitable lighting
- Plain background behind the object

### Optional hardware

- A4988 or DRV8825 stepper motor driver
- Stepper motor and turntable mechanism
- MPU-6050 accelerometer and gyroscope
- FAT32-formatted microSD card
- External light connected to a supported GPIO

## ESP32-S3 Camera Wiring

The sketches use the following camera pin layout:

| Camera Signal | ESP32-S3 GPIO |
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

Pin assignments vary between ESP32-S3 camera boards. Check the board schematic before connecting hardware.

## Stepper Motor Wiring

The `3d_scanner.ino` sketch uses these default stepper-driver pins:

| Driver Signal | ESP32-S3 GPIO |
|---|---:|
| STEP | GPIO 41 |
| DIR | GPIO 42 |
| EN | GPIO 47 |
| GND | Board GND |

The enable pin is active-low. The motor driver is enabled by pulling GPIO 47 low.

The default motor settings are:

```cpp
#define MOTOR_STEPS 200
#define MICROSTEP   16
#define GEAR_RATIO  1.0
#define STEP_US     600
#define ROT_DIR     1
```

Change these values in `3d_scanner.ino` to match the motor, driver, microstepping configuration, and turntable gearing.

If your display or another peripheral uses GPIO 41, 42, or 47, select different free GPIO pins in the sketch.

### Manual Rotation

A motor is not required. Enable **Manual rotation** in the web interface, rotate the object by the requested angle, and press **Next view** for each image.

## microSD Wiring

The `tempad_camera_sd_webui.ino` sketch uses 1-bit SD_MMC mode:

| SD Signal | ESP32-S3 GPIO |
|---|---:|
| CLK | GPIO 39 |
| CMD | GPIO 38 |
| D0 | GPIO 40 |

Use a FAT32-formatted microSD card.

The microSD card is used by `tempad_camera_sd_webui.ino` for photo storage and file management. The main `3d_scanner.ino` sketch downloads the STL directly through the browser and does not require a microSD card.

## MPU-6050 Wiring

The `mpu6050_test.ino` sketch uses I2C:

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

If AD0 is connected to 3.3V, the address becomes `0x69`. Update `MPU_ADDR` in the sketch if necessary.

The MPU-6050 is currently provided as a separate diagnostic sketch. It is not synchronized with the camera captures in the current implementation.

## Arduino IDE Configuration

Install the ESP32 Arduino board package and select:

- **Board:** ESP32S3 Dev Module
- **USB CDC On Boot:** Enabled
- **PSRAM:** OPI PSRAM
- **Flash Size:** 16MB
- **Partition Scheme:** Huge APP or a 16MB partition with sufficient application space
- **Serial Monitor Baud Rate:** 115200

For `tempad_camera_sd_webui.ino`, the original configuration uses a partition scheme similar to:

```text
16M Flash, 3MB APP, 9.9MB FATFS
```

The sketches use libraries included with the ESP32 Arduino board package. No additional frontend or third-party Arduino libraries are required.

## Installation

1. Install Arduino IDE.
2. Install the ESP32 board package.
3. Connect the ESP32-S3 board.
4. Open the desired `.ino` sketch.
5. Verify the camera pin definitions for your board.
6. Configure Wi-Fi credentials.
7. Select the correct ESP32-S3 board and serial port.
8. Enable OPI PSRAM.
9. Upload the sketch.
10. Open the Serial Monitor at `115200` baud.
11. Open the IP address printed by the board in a web browser.

## Wi-Fi Configuration

### `3d_scanner.ino`

Edit the Wi-Fi settings near the top of the sketch:

```cpp
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
```

The sketch falls back to an access point if it cannot connect to the configured network:

```cpp
#define AP_SSID "Tempad-Scanner"
#define AP_PASS "tempad1234"
```

### `tempad_camera_sd_webui.ino`

Add one or more networks inside `addWifiNetworks()`:

```cpp
void addWifiNetworks() {
  wifiMultiAdd("YOUR_WIFI_NAME", "YOUR_WIFI_PASSWORD");
}
```

If no configured network is available, the board creates this access point:

```text
SSID: Tempad-ESP32
Password: tempad1234
```

Change the fallback password before using the device outside a trusted environment.

## Using the 3D Scanner

After uploading `3d_scanner.ino`:

1. Open the IP address printed in the Serial Monitor.
2. Confirm that the camera image is upright and not mirrored.
3. Place the object in the center of the turntable.
4. Use a plain background with good contrast.
5. Capture the empty background.
6. Use **Test mask** to verify object detection.
7. Adjust the mask threshold if necessary.
8. Select the number of views.
9. Select automatic or manual rotation.
10. Start the scan.
11. Choose the voxel resolution and smoothing amount.
12. Build the model.
13. Inspect the browser preview.
14. Download the generated STL file.

### Recommended scanning conditions

- Keep the camera fixed during the scan.
- Keep the object centered on the turntable axis.
- Use even, stable lighting.
- Avoid shadows on the background.
- Avoid reflective or transparent objects.
- Lock exposure and white balance after the image looks correct.
- Ensure the entire object remains inside the configured scan radius and height.

## 3D Scanner Settings

The web interface provides controls for:

- Camera resolution
- JPEG quality
- Exposure and white-balance locking
- Camera-to-turntable distance
- Camera height
- Horizontal field of view
- Scan radius
- Scan height
- Number of views
- Rotation settling time
- Manual or motorized rotation
- Silhouette threshold
- Largest-object filtering
- Voxel grid resolution
- Mesh smoothing passes

Higher voxel resolutions produce more detailed models but require more browser memory and processing time.

## Camera Web Interface

The `tempad_camera_sd_webui.ino` sketch provides:

- Live camera streaming
- Camera resolution selection
- Photo capture
- Automatic photo storage in `/photos/`
- SD card browsing
- Folder creation
- File upload
- File download
- File deletion
- Folder deletion when empty
- SD card storage information

The main web interface runs on port `80`.

The camera stream runs on port `81`:

```text
http://<ESP32-IP-ADDRESS>:81/stream
```

The device may also be available through mDNS:

```text
http://tempad.local
```

Captured photos are stored using names such as:

```text
/photos/IMG_0.jpg
/photos/IMG_1.jpg
/photos/IMG_2.jpg
```

## Testing the MPU-6050

Upload `mpu6050_test.ino` to test the motion sensor.

The sketch:

1. Scans the I2C bus.
2. Reads the MPU-6050 `WHO_AM_I` register.
3. Wakes the sensor from sleep mode.
4. Configures the accelerometer and gyroscope.
5. Calibrates the gyroscope.
6. Continuously prints acceleration, angular velocity, and temperature.

Example output:

```text
Accel(g) X= 0.01 Y=-0.02 Z= 0.98 | Gyro(dps) X= 0.12 Y=-0.05 Z= 0.03 | Temp=25.4 C
```

Keep the MPU-6050 stationary during calibration.

## Troubleshooting

### Camera initialization failed

Check the following:

- The camera ribbon cable is fully inserted.
- The camera pin definitions match your board.
- OPI PSRAM is enabled.
- The correct ESP32-S3 board is selected.
- The camera module is compatible with the configured pin layout.
- The board has a stable 5V or USB power source.

### PSRAM was not detected

Check:

- **Tools > PSRAM > OPI PSRAM** is selected.
- The selected board is an ESP32-S3 board with PSRAM.
- The partition and board settings match the hardware.

Without PSRAM, the sketches fall back to lower camera resolutions and fewer frame buffers.

### SD card mount failed

Check:

- The card is inserted correctly.
- The card is formatted as FAT32.
- GPIO 38, 39, and 40 match the board wiring.
- The card is not damaged or write-protected.
- The card capacity is supported by the board and filesystem configuration.

### The 3D model is empty

Try the following:

- Capture the empty background again.
- Use a background with stronger contrast.
- Adjust the mask threshold.
- Make sure the object is visible in every view.
- Verify the camera is aimed at the turntable center.
- Check the camera-to-turntable distance.
- Try enabling **Reverse rotation direction**.
- Reduce the scan radius or scan height.
- Use more views.

### The model contains unwanted background

Try the following:

- Improve lighting uniformity.
- Remove shadows behind the object.
- Use a plain background.
- Increase the silhouette threshold.
- Enable **Keep largest blob only**.
- Move the object farther from the background.

### The turntable rotates incorrectly

Check:

- STEP, DIR, EN, and GND wiring.
- The selected microstepping setting.
- `MOTOR_STEPS`.
- `GEAR_RATIO`.
- `ROT_DIR`.
- Whether GPIO 41, 42, or 47 is being used by another peripheral.
- The motor driver's power supply and current limit.

### No MPU-6050 device is found

Check:

- VCC is connected to 3.3V.
- GND is connected correctly.
- SDA is connected to GPIO 1.
- SCL is connected to GPIO 2.
- AD0 is connected to GND for address `0x68`.
- The I2C lines have suitable pull-up resistors.
- The correct I2C address is configured.

### MPU-6050 readings are zero

Possible causes include:

- The sensor is still in sleep mode.
- Incorrect wiring.
- Incorrect I2C address.
- A faulty or incompatible sensor module.
- Loose connections.
- Insufficient power.

## Security Considerations

The web interfaces are intended for use on a trusted local network.

They do not provide:

- User authentication
- HTTPS encryption
- Access control
- Secure file permissions
- Protection against unauthorized file uploads or downloads

Do not expose the ESP32 web interface directly to the public internet.

Do not commit real Wi-Fi passwords to the repository. Replace credentials with placeholders before sharing or publishing the sketches.

## Current Limitations

- The 3D reconstruction runs in the browser and can be demanding at high resolutions.
- The scanner currently creates visual-hull models rather than full photogrammetry models.
- Concave areas that are not visible from the camera may be reconstructed incorrectly.
- Transparent, reflective, and low-contrast objects may produce poor silhouettes.
- Camera captures and MPU-6050 readings are not currently synchronized.
- The MPU-6050 is currently tested through a separate sketch.
- The scanner does not currently store synchronized image and sensor datasets.
- Camera pin mappings may need to be changed for different ESP32-S3 boards.
- The fallback Wi-Fi access point uses a default password unless changed.
- Deleting non-empty folders is not supported by the SD card web interface.
- The STL file is downloaded by the browser and is not saved directly to the ESP32-S3.
- The turntable requires suitable mechanical alignment for accurate models.

## Possible Future Improvements

- Combine camera capture and MPU-6050 recording into one sketch.
- Add timestamps to camera frames and sensor readings.
- Store synchronized image and motion data as CSV or JSON.
- Add automatic capture intervals.
- Add motion-triggered image capture.
- Improve camera calibration and lens correction.
- Add camera pose estimation.
- Improve silhouette segmentation.
- Add texture capture.
- Add a desktop or Python reconstruction tool.
- Add authentication to the web interface.
- Add support for exporting synchronized scan datasets.
- Add support for additional 3D mesh formats.
- Add automatic turntable calibration.
- Improve STL mesh cleanup and hole repair.

## License

No license has currently been specified for this repository. Add a license file if you plan to distribute, modify, or reuse the project publicly.
