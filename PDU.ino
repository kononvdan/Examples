#include <EEPROM.h>

#include "ssd1306.h"
#include "nano_gfx.h"


// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

constexpr uint8_t RELAY_PINS[] = {2, 3, 4, 5, 6, 7};
constexpr uint8_t JOYSTICK_PINS[] = {A0, A1};

constexpr size_t RELAY_COUNT = sizeof(RELAY_PINS) / sizeof(RELAY_PINS[0]);

constexpr int JOYSTICK_LOW_THRESHOLD = 200;
constexpr int JOYSTICK_HIGH_THRESHOLD = 800;

constexpr unsigned long JOYSTICK_INTERVAL_MS = 100;
constexpr unsigned long SERIAL_TIMEOUT_MS = 50;


// -----------------------------------------------------------------------------
// Types
// -----------------------------------------------------------------------------

enum class Command {
  Set,
  Get,
  Unknown
};

enum class RelayState {
  On = LOW,
  Off = HIGH
};

enum class JoystickState {
  Up,
  Down,
  Left,
  Right,
  None
};


// -----------------------------------------------------------------------------
// UI
// -----------------------------------------------------------------------------

SAppMenu mainMenu;
SAppMenu pduMenu;

SAppMenu* currentMenu = &mainMenu;

const char* mainMenuItems[] = {
  "PDU",
  "Journal",
};

String pduMenuItemValues[RELAY_COUNT];

char* pduMenuItems[RELAY_COUNT + 1] = {
  "Back"
};


// -----------------------------------------------------------------------------
// Command parser
// -----------------------------------------------------------------------------

String args[4];


// -----------------------------------------------------------------------------
// Runtime state
// -----------------------------------------------------------------------------

JoystickState previousJoystickState = JoystickState::None;

unsigned long lastJoystickUpdate = 0;


// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------

void initPins();
void initDisplay();

void listenCommands();
void parseInput(const String& input);

Command parseCommand(const String& command);

void handleManualControl();
JoystickState readJoystick();

void setRelay(size_t index, RelayState state);
RelayState getRelayState(size_t index);

void showCurrentMenu();
void showMenu(SAppMenu* menu);

void updatePduMenuItem(size_t index, RelayState state);


// -----------------------------------------------------------------------------
// Setup / Loop
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(9600);

  initPins();
  initDisplay();

  Serial.println("READY");
}


void loop() {
  listenCommands();

  const unsigned long now = millis();

  if (now - lastJoystickUpdate >= JOYSTICK_INTERVAL_MS) {
    lastJoystickUpdate = now;
    handleManualControl();
  }
}


// -----------------------------------------------------------------------------
// Initialization
// -----------------------------------------------------------------------------

void initPins() {
  for (size_t i = 0; i < RELAY_COUNT; ++i) {
    pinMode(RELAY_PINS[i], OUTPUT);

    const RelayState state = getRelayState(i);

    digitalWrite(RELAY_PINS[i], static_cast<uint8_t>(state));

    Serial.print(state == RelayState::On ? 1 : 0);

    updatePduMenuItem(i, state);
  }

  Serial.println();
}


void initDisplay() {
  ssd1306_setFixedFont(ssd1306xled_font6x8);
  ssd1306_128x64_i2c_init();
  ssd1306_clearScreen();

  ssd1306_createMenu(
      &mainMenu,
      mainMenuItems,
      sizeof(mainMenuItems) / sizeof(mainMenuItems[0]));

  ssd1306_createMenu(
      &pduMenu,
      pduMenuItems,
      sizeof(pduMenuItems) / sizeof(pduMenuItems[0]));

  currentMenu = &mainMenu;

  showCurrentMenu();
}


// -----------------------------------------------------------------------------
// Relay control
// -----------------------------------------------------------------------------

void setRelay(size_t index, RelayState state) {
  // Protect against invalid indexes.
  if (index >= RELAY_COUNT) {
    Serial.println("ERR: invalid relay index");
    return;
  }

  digitalWrite(RELAY_PINS[index], static_cast<uint8_t>(state));

  // EEPROM.update() only writes when the value has changed.
  EEPROM.update(RELAY_PINS[index], static_cast<uint8_t>(state));

  Serial.print(index);
  Serial.print(" - ");
  Serial.println(state == RelayState::On ? "ON" : "OFF");

  updatePduMenuItem(index, state);

  ssd1306_updateMenu(currentMenu);
  ssd1306_showMenu(currentMenu);
}


RelayState getRelayState(size_t index) {
  if (index >= RELAY_COUNT) {
    return RelayState::Off;
  }

  const uint8_t value = EEPROM.read(RELAY_PINS[index]);

  return value == static_cast<uint8_t>(RelayState::On)
             ? RelayState::On
             : RelayState::Off;
}


// -----------------------------------------------------------------------------
// Menu
// -----------------------------------------------------------------------------

void showCurrentMenu() {
  ssd1306_clearScreen();
  ssd1306_showMenu(currentMenu);
}


void showMenu(SAppMenu* menu) {
  currentMenu = menu;
  showCurrentMenu();
}


void updatePduMenuItem(size_t index, RelayState state) {
  if (index >= RELAY_COUNT) {
    return;
  }

  pduMenuItemValues[index] =
      String(index) + " - " + (state == RelayState::On ? "ON" : "OFF");

  pduMenuItems[index + 1] = pduMenuItemValues[index].c_str();
}


// -----------------------------------------------------------------------------
// Serial commands
// -----------------------------------------------------------------------------

void listenCommands() {
  if (Serial.available() <= 0) {
    return;
  }

  const String input = Serial.readString();

  parseInput(input);

  const Command command = parseCommand(args[0]);

  switch (command) {
    case Command::Set: {
      if (args[1].length() == 0 || args[2].length() == 0) {
        Serial.println("ERR: invalid SET arguments");
        return;
      }

      const int index = args[1].toInt();

      if (index < 0 || index >= static_cast<int>(RELAY_COUNT)) {
        Serial.println("ERR: invalid relay index");
        return;
      }

      RelayState state;

      if (args[2] == "ON" || args[2] == "UP") {
        state = RelayState::On;
      } else if (args[2] == "OFF" || args[2] == "DOWN") {
        state = RelayState::Off;
      } else {
        Serial.println("ERR: invalid relay state");
        return;
      }

      setRelay(static_cast<size_t>(index), state);
      break;
    }

    case Command::Get: {
      for (size_t i = 0; i < RELAY_COUNT; ++i) {
        Serial.print(
            getRelayState(i) == RelayState::On ? 1 : 0
        );
      }

      Serial.println();
      break;
    }

    case Command::Unknown:
      Serial.println("ERR: unknown command");
      break;
  }
}


Command parseCommand(const String& command) {
  if (command == "SET") {
    return Command::Set;
  }

  if (command == "GET") {
    return Command::Get;
  }

  return Command::Unknown;
}


void parseInput(const String& input) {
  // Clear previous arguments.
  for (String& arg : args) {
    arg = "";
  }

  String token;
  size_t argIndex = 0;

  String normalizedInput = input;
  normalizedInput.trim();
  normalizedInput.toUpperCase();

  for (size_t i = 0; i < normalizedInput.length(); ++i) {
    const char character = normalizedInput[i];

    if (character != ';') {
      token += character;
      continue;
    }

    if (argIndex < sizeof(args) / sizeof(args[0])) {
      args[argIndex] = token;
      ++argIndex;
    }

    token = "";
  }

  // Store the last argument.
  if (argIndex < sizeof(args) / sizeof(args[0])) {
    args[argIndex] = token;
  }
}


// -----------------------------------------------------------------------------
// Joystick
// -----------------------------------------------------------------------------

JoystickState readJoystick() {
  const int x = analogRead(JOYSTICK_PINS[0]);
  const int y = analogRead(JOYSTICK_PINS[1]);

  // Horizontal direction has priority over vertical direction,
  // preserving the behavior of the original implementation.
  if (x >= JOYSTICK_HIGH_THRESHOLD) {
    return JoystickState::Right;
  }

  if (x <= JOYSTICK_LOW_THRESHOLD) {
    return JoystickState::Left;
  }

  if (y >= JOYSTICK_HIGH_THRESHOLD) {
    return JoystickState::Down;
  }

  if (y <= JOYSTICK_LOW_THRESHOLD) {
    return JoystickState::Up;
  }

  return JoystickState::None;
}


void handleManualControl() {
  const JoystickState currentState = readJoystick();

  // Ignore repeated events while the joystick is held.
  if (currentState == previousJoystickState) {
    return;
  }

  previousJoystickState = currentState;

  switch (currentState) {
    case JoystickState::Right: {
      if (currentMenu == &mainMenu) {
        const int option = ssd1306_menuSelection(&mainMenu);

        switch (option) {
          case 0:
            showMenu(&pduMenu);
            break;

          case 1:
            // TODO: Open journal.
            break;

          default:
            break;
        }
      } else if (currentMenu == &pduMenu) {
        const int option = ssd1306_menuSelection(&pduMenu);

        if (option == 0) {
          showMenu(&mainMenu);
          break;
        }

        const size_t relayIndex = static_cast<size_t>(option - 1);

        if (relayIndex < RELAY_COUNT) {
          const RelayState currentState = getRelayState(relayIndex);

          const RelayState newState =
              currentState == RelayState::On
                  ? RelayState::Off
                  : RelayState::On;

          setRelay(relayIndex, newState);
        }
      }

      break;
    }


    case JoystickState::Left: {
      if (currentMenu == &pduMenu) {
        showMenu(&mainMenu);
      }

      break;
    }


    case JoystickState::Up:
      ssd1306_menuUp(currentMenu);
      ssd1306_updateMenu(currentMenu);
      break;


    case JoystickState::Down:
      ssd1306_menuDown(currentMenu);
      ssd1306_updateMenu(currentMenu);
      break;


    case JoystickState::None:
      // Nothing to do.
      break;
  }
}
