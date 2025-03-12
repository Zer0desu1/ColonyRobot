import processing.serial.*;

Serial myPort;
final int BAUD_RATE = 115200;
final int WINDOW_WIDTH = 800;
final int WINDOW_HEIGHT = 600;

// Robot parameters
final int ROBOT_SIZE = 40;
float robotX = WINDOW_WIDTH / 4;
float robotY = WINDOW_HEIGHT / 2;
float robotAngle = 0;

// Wall parameters
final int WALL_THICKNESS = 20;
final int WALL_X = 3 * WINDOW_WIDTH / 4;

// Sensor readings
float distanceToWall = 150; // mm
float magnetometerX = 0;
float magnetometerY = 0;
float magnetometerZ = 0;
float accelerometerX = 0;
float accelerometerY = 0;
float accelerometerZ = 0;
void settings() {
  size(800, 600); // Move size() here
}
void setup() {
  println("Available serial ports:");
  printArray(Serial.list());
  
  // Use the appropriate port number from the printed list
  String portName = Serial.list()[0]; // Change this index as needed
  myPort = new Serial(this, portName, BAUD_RATE);
  myPort.bufferUntil('\n');
}

void draw() {
  background(240);

  // Draw wall
  fill(150);
  rect(WALL_X, 0, WALL_THICKNESS, WINDOW_HEIGHT);

  // Draw robot
  pushMatrix();
  translate(robotX, robotY);
  rotate(robotAngle);

  // Robot body
  fill(0, 150, 255);
  rect(-ROBOT_SIZE / 2, -ROBOT_SIZE / 2, ROBOT_SIZE, ROBOT_SIZE);

  // Sensor indicator
  fill(255, 0, 0);
  rect(ROBOT_SIZE / 2, -2, 5, 4);

  popMatrix();

  // Draw sensor line
  stroke(255, 0, 0, 100);
  float sensorX = robotX + cos(robotAngle) * ROBOT_SIZE / 2;
  float sensorY = robotY + sin(robotAngle) * ROBOT_SIZE / 2;
  float wallX = sensorX + cos(robotAngle) * (distanceToWall + ROBOT_SIZE / 2);
  float wallY = sensorY + sin(robotAngle) * (distanceToWall + ROBOT_SIZE / 2);
  line(sensorX, sensorY, wallX, wallY);

  // Display sensor readings
  fill(0);
  textSize(14);
  textAlign(LEFT);

  text("Distance to Wall: " + nf(distanceToWall, 0, 1) + " mm", 20, 30);
  text("Magnetometer:", 20, 60);
  text("X: " + nf(magnetometerX, 0, 2), 40, 80);
  text("Y: " + nf(magnetometerY, 0, 2), 40, 100);
  text("Z: " + nf(magnetometerZ, 0, 2), 40, 120);
  text("Accelerometer:", 20, 150);
  text("X: " + nf(accelerometerX, 0, 2), 40, 170);
  text("Y: " + nf(accelerometerY, 0, 2), 40, 190);
  text("Z: " + nf(accelerometerZ, 0, 2), 40, 210);
}

void serialEvent(Serial port) {
  String input = port.readStringUntil('\n');
  if (input != null) {
    input = trim(input);

    try {
      if (input.startsWith("Distance")) {
        String[] parts = split(input, ": ");
        if (parts.length == 2) {
          distanceToWall = float(parts[1]);
          updateRobotPosition();
        }
      } else if (input.startsWith("Magnetometer")) {
        String[] parts = match(input, "X: (-?\\d+\\.\\d+) Y: (-?\\d+\\.\\d+) Z: (-?\\d+\\.\\d+)");
        if (parts != null && parts.length == 4) {
          magnetometerX = float(parts[1]);
          magnetometerY = float(parts[2]);
          magnetometerZ = float(parts[3]);
        }
      } else if (input.startsWith("Accelerometer")) {
        String[] parts = match(input, "X: (-?\\d+\\.\\d+) Y: (-?\\d+\\.\\d+) Z: (-?\\d+\\.\\d+)");
        if (parts != null && parts.length == 4) {
          accelerometerX = float(parts[1]);
          accelerometerY = float(parts[2]);
          accelerometerZ = float(parts[3]);
        }
      }
    } catch (Exception e) {
      println("Error parsing input: " + input);
    }
  }
}

void updateRobotPosition() {
  float targetDistance = 150;
  float tolerance = 60;
  float speed = 2;

  float error = distanceToWall - targetDistance;

  if (abs(error) > tolerance) {
    robotAngle += (error > 0) ? 0.02 : -0.02;
  }

  // Constrain robot angle to avoid excessive rotations
  robotAngle = constrain(robotAngle, -PI / 2, PI / 2);

  // Update robot position
  robotX += cos(robotAngle) * speed;
  robotY += sin(robotAngle) * speed;

  // Keep robot within bounds
  robotX = constrain(robotX, ROBOT_SIZE, WALL_X - ROBOT_SIZE);
  robotY = constrain(robotY, ROBOT_SIZE, WINDOW_HEIGHT - ROBOT_SIZE);

  // Reset position if robot hits the wall bounds
  if (robotX <= ROBOT_SIZE || robotX >= WALL_X - ROBOT_SIZE) {
    resetRobotPosition();
  }
}

void resetRobotPosition() {
  robotX = WINDOW_WIDTH / 4;
  robotY = WINDOW_HEIGHT / 2;
  robotAngle = 0;
}
