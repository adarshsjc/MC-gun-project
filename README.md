# Motion-Controlled Gaming Gun (STM32 Blue Pill Edition)

This project is a motion-controlled gaming gun built with an **STM32F103C8T6 (Blue Pill)** and an **MPU-6050** gyro/accelerometer module. It allows you to play FPS games directly in your browser using physical movements to aim, alongside a joystick and buttons for movement and actions.

## Hardware Components
- **Microcontroller**: STM32F103C8T6 (Blue Pill)
- **Motion Sensor**: MPU-6050 / MPU-6500 (Gyroscope + Accelerometer via I2C)
- **Movement**: Analog Joystick (X/Y axes)
- **Actions**: 3x Push Buttons (Shoot, Reload, Respawn/Recenter)
- **Data Connection**: USB CDC (Direct Micro-USB to PC Serial connection)

## The Game: Operation Ironhold
This repository includes a modified version of **Operation Ironhold**, an open-source browser-based FPS game, adapted specifically to work with this gun controller. 

**Original Game Creator**: [StarKnightt](https://github.com/StarKnightt)  
**Original Game Repository**: [operation-ironhold](https://github.com/StarKnightt/operation-ironhold)

A huge thanks to the creator of Operation Ironhold for making the game open-source and providing an amazing foundation for this hardware integration!

## How it Works
1. The **STM32 Blue Pill** reads the MPU-6050 sensor data (pitch/yaw rate) via I2C, and the state of the joystick and buttons via ADC/GPIO.
2. The STM32 sends this data over a native USB CDC serial connection as JSON strings.
3. The Python Bridge Server (ridge_server.py) auto-detects the COM port, opens a WebSocket server, and relays the hardware data to the browser game.
4. The modified index.html of Operation Ironhold connects to the WebSocket, processes the real-time sensor data, and translates it into in-game aiming (mouse movement), walking (analog WASD), and shooting actions.

## Setup Instructions
1. Flash the STM32 code located in src/main.cpp using PlatformIO. *(Note: You can use an ST-Link to flash, but you MUST keep the platformio.ini USB flags intact so the Micro-USB port works as a Serial port!)*
2. Connect the STM32 Blue Pill directly to your PC via a Micro-USB data cable.
3. Install the required Python dependencies: pip install pyserial websockets
4. Run the Python bridge server: python python_scripts/bridge_server.py
5. Open the operation-ironhold/index.html file in your browser via the local server running on http://localhost:8080.
6. Select **GUN CONTROLLER** on the start screen.

*(Wiring diagrams and pictures coming soon!)*
