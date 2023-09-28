#include <Arduino.h>

#include "HomeSpan.h" 
#include "Wire.h"
#include <Adafruit_NeoPixel.h>

#include <WiFi.h>
#include "driver/rmt.h"
#include "esp_log.h"

//////////////////////////////////////////////

constexpr const char* versionString = "v0.1";

constexpr const char* accessoryName = "Front Shade";

constexpr const char* displayName = "Shade-Controller";
constexpr const char* modelName = "Shade-Controller-ESP32";

constexpr uint8_t indicatorDataPin = PIN_NEOPIXEL;
constexpr uint8_t indicatorPowerPin = NEOPIXEL_POWER;

constexpr uint8_t controlOutputPin = MISO;
constexpr uint8_t directionOutputPin = MOSI;

constexpr uint8_t openSensePin = A1;
constexpr uint8_t closeSensePin = A0;

constexpr uint8_t openPin = SCK;
constexpr uint8_t closePin = RX;

//////////////////////////////////////////////

constexpr float homeKitFullScale = 100.0;

constexpr float homeKitShadeOpenValue = 100.0;
constexpr float homeKitShadeClosedValue = 0.0;

constexpr uint8_t homeKitPositionStateClosing = 0;
constexpr uint8_t homeKitPositionStateOpening = 1;
constexpr uint8_t homeKitPositionStateStopped = 2;

//////////////////////////////////////////////

constexpr uint32_t indicatorTimeoutMS = 10 * 1000;

constexpr uint32_t closeTimeMS = 10 * 1000;
constexpr uint32_t openTimeMS = 10 * 1000;

constexpr uint32_t endStopExtraTime = 500;

constexpr float closePerMS = homeKitFullScale / closeTimeMS;
constexpr float openPerMS = homeKitFullScale / openTimeMS;

constexpr uint32_t updateTimeMS = 500;
constexpr uint32_t quickUpdateTimeMS = 100;

typedef uint16_t ShadeState;
constexpr uint16_t shadeStateOpen = 0;
constexpr uint16_t shadeStateStopped = 1;
constexpr uint16_t shadeStateClosed = 2;

constexpr uint16_t shadeStateClosing = 1 << 3;
constexpr uint16_t shadeStateOpening = 1 << 4;

constexpr uint16_t shadeStateUserAction = 1 < 5;
constexpr uint16_t shadeStateLocalAction = 1 << 6;
constexpr uint16_t shadeStateHomeKitAction = 1 << 7;

typedef uint16_t OutputState;
constexpr uint16_t outputStateIdle = 0;
constexpr uint16_t outputStateOpen = 1;
constexpr uint16_t outputStateClose = 2;

//////////////////////////////////////////////

bool wifiConnected = false;
bool hadWifiConnection = false;

#if defined(DEBUG)
// #define SerPrintf(...) Serial.printf(__VA_ARGS__)
#define SerPrintf(...) Serial.printf("%d: ", millis()); Serial.printf(__VA_ARGS__)
#define SerBegin(...) Serial.begin(__VA_ARGS__)
#else
#define SerPrintf(...)
#define SerBegin(...)
#endif

//////////////////////////////////////////////

uint64_t millis64() {
	volatile static uint32_t low32 = 0, high32 = 0;
	uint32_t new_low32 = millis();

	if (new_low32 < low32)
		high32++;

	low32 = new_low32;

	return (uint64_t) high32 << 32 | low32;
}

//////////////////////////////////////////////

constexpr uint8_t ledStatusLevel = 0x10;
constexpr uint32_t whiteColorFlag = 1 << 24;

#define MAKE_RGB(r, g, b) (r<<16 | g<<8 | b)
#define MAKE_RGB_STATUS(r, g, b) ((r*ledStatusLevel)<<16 | (g*ledStatusLevel)<<8 | (b*ledStatusLevel))
#define R_COMP(c) ((c>>16) & 0xFF)
#define G_COMP(c) ((c>>8) & 0xFF)
#define B_COMP(c) (c & 0xFF)

constexpr uint32_t blackColor = MAKE_RGB_STATUS(0, 0, 0);
constexpr uint32_t flashColor = MAKE_RGB_STATUS(1, 0, 1);
constexpr uint32_t startColor = MAKE_RGB_STATUS(0, 1, 0);
constexpr uint32_t connectingColor = MAKE_RGB_STATUS(0, 0, 1);
constexpr uint32_t offColor = MAKE_RGB_STATUS(0, 1, 1);
constexpr uint32_t sendColor = MAKE_RGB_STATUS(1, 1, 1);

constexpr uint32_t openColor = MAKE_RGB(1, 0, 0);
constexpr uint32_t closedColor = MAKE_RGB(1, 1, 0);

uint32_t makeShadeColor(float level) {
	constexpr uint8_t ledBright = 30;
	constexpr uint8_t rL = ledBright * R_COMP(openColor);
	constexpr uint8_t gL = ledBright * G_COMP(openColor);
	constexpr uint8_t bL = ledBright * B_COMP(openColor);
	constexpr uint8_t rH = ledBright * R_COMP(closedColor);
	constexpr uint8_t gH = ledBright * G_COMP(closedColor);
	constexpr uint8_t bH = ledBright * B_COMP(closedColor);
	
	uint16_t r = rL + (rH - rL) * level;
	uint16_t g = gL + (gH - gL) * level;
	uint16_t b = bL + (bH - bL) * level;

	return MAKE_RGB(r, g, b);
}

//////////////////////////////////////////////

Adafruit_NeoPixel indicator(1, indicatorDataPin);
uint32_t currentIndicatorColor = 0xFFFFFFFF;

void setIndicator(uint32_t color) {
	if (color != currentIndicatorColor) {
		currentIndicatorColor = color;

		if (color & whiteColorFlag) {
			color &= 0xFF;
			indicator.setPixelColor(0, color, color, color);
		}
		else {
			indicator.setPixelColor(0, color>>16, (color >> 8) & 0xFF, color & 0xFF);
		}
		indicator.show();
	}
}

void setIndicator8(uint8_t color8) {
	setIndicator(whiteColorFlag | color8);
}

void flashIndicator(uint32_t color, uint16_t count, uint16_t period) {
	for (auto i=0; i<count; i++) {
		setIndicator(color);
		delay(period/4);
		setIndicator(0);
		delay(period*3/4);
	}
}

void updateIndicator(ShadeState state, float shadePosition) {
	static uint64_t lastStoppedTime = 0;
	uint64_t curTime = millis64();
	uint32_t color;
	bool moving = (state & (shadeStateOpening | shadeStateClosing)) != 0;

	if ((lastStoppedTime == 0) || moving) {
		lastStoppedTime = curTime;
	}

	if (!wifiConnected && ((millis()%600) < 200)) {
		color = connectingColor;
	}
	else if (moving && ((millis()%200) < 30)) {
		color = blackColor;
	}
	else if (curTime - lastStoppedTime >= indicatorTimeoutMS) {
		color = blackColor;
	}
	else {
		color = makeShadeColor(shadePosition);
	}

	setIndicator(color);
}

//////////////////////////////////////////////

constexpr uint32_t debounceTimeMS = 10;

class ButtonBase {
	public:
		ButtonBase(uint8_t pin) : _pin(pin) {}
		virtual bool isPressed() { return false; };
		bool pressed() {
			bool result = false;
			bool currentPress = isPressed();

			if (currentPress) {
				if (_pressed) {
					result = (millis64()-_pressTime) > debounceTimeMS;
				}
				else {
					_pressed = true;
					_pressTime = millis64();
				}
			}
			else {
				_pressed = false;
			}
			return result;
		}

	protected:
		uint8_t _pin;
		bool _pressed = false;
		uint64_t _pressTime;
};

class ButtonDigital : public ButtonBase {
	public:
		ButtonDigital(uint8_t pin) : ButtonBase(pin) {
			pinMode(_pin, INPUT_PULLUP);
		};
		bool isPressed() { return digitalRead(_pin)==LOW; };
};

constexpr uint16_t analogInputThreshold = 500;

class ButtonAnalog : public ButtonBase {
	public:
		ButtonAnalog(uint8_t pin) : ButtonBase(pin) {};
		bool isPressed() { return analogRead(_pin) > analogInputThreshold; };
};

//////////////////////////////////////////////

struct RVShade : Service::WindowCovering {
	SpanCharacteristic* _positionState = NULL;
	SpanCharacteristic* _currentPosition = NULL;
	SpanCharacteristic* _targetPosition = NULL;
	SpanCharacteristic* _holdPosition = NULL;

	ButtonDigital* _localOpenButton = NULL;
	ButtonDigital* _localCloseButton = NULL;

	ButtonAnalog* _userOpenButton = NULL;
	ButtonAnalog* _userCloseButton = NULL;

	ShadeState _shadeState = shadeStateOpen;
	float _currentShadePosition = homeKitShadeOpenValue;
	uint64_t _nextPositionUpdateTime = 0;

	OutputState _outputState = outputStateIdle;

	uint64_t _operationEndTime;

	bool _doHoldPosition = false;

	RVShade() : Service::WindowCovering() {
		_positionState = new Characteristic::PositionState(homeKitPositionStateStopped);
		_currentPosition = new Characteristic::CurrentPosition(homeKitShadeOpenValue);
		_targetPosition = new Characteristic::TargetPosition(homeKitShadeOpenValue);
		_holdPosition = new Characteristic::HoldPosition();

		_localOpenButton = new ButtonDigital(openPin);
		_localCloseButton = new ButtonDigital(closePin);
		_userOpenButton = new ButtonAnalog(openSensePin);
		_userCloseButton = new ButtonAnalog(closeSensePin);

		digitalWrite(controlOutputPin, LOW);
		pinMode(controlOutputPin, OUTPUT);
		digitalWrite(directionOutputPin, LOW);
		pinMode(directionOutputPin, OUTPUT);
	}

	boolean update() {
		uint64_t curTime = millis64();

		if (_targetPosition->updated()) {
			float targetValue = _targetPosition->getNewVal<float>();
			float moveAmount = targetValue - _currentShadePosition;
			uint32_t moveTimeMS = abs(moveAmount / homeKitFullScale) * ((moveAmount > 0) ? openTimeMS : closeTimeMS);

			_operationEndTime = curTime + moveTimeMS;

			if (targetValue == homeKitShadeClosedValue || targetValue == homeKitShadeOpenValue) {
				_operationEndTime += endStopExtraTime;
			}

			_shadeState = shadeStateHomeKitAction | ((moveAmount > 0) ? shadeStateOpening : shadeStateClosing);

			SerPrintf("Changing position from %0.1f%% to %0.1f%% over %dmS.\n", _currentShadePosition, targetValue, (int)(_operationEndTime-curTime));
			SerPrintf("State changed to 0x%02X\n", _shadeState);
		}
		if (_holdPosition && _holdPosition->updated()) {
			SerPrintf("Hold position.\n");
			_doHoldPosition = true;
		}

		return true;
	}

	void loop() {
		static uint64_t lastTime = 0;
		uint64_t curTime = millis64();
		ShadeState newShadeState;

		if (lastTime == 0) {
			lastTime = curTime;
		}

		if (curTime > lastTime) {
			bool userOpen = false; // _userOpenButton->pressed();
			bool userClose = false; // _userCloseButton->pressed();
			bool localOpen = _localOpenButton->pressed();
			bool localClose = _localCloseButton->pressed();
			bool updateState = false;

			if (userOpen) {
				newShadeState = shadeStateOpening | shadeStateUserAction;
			}
			else if (userClose) {
				newShadeState = shadeStateClosing | shadeStateUserAction;
			}
			else if (localOpen) {
				newShadeState = shadeStateOpening | shadeStateLocalAction;
			}
			else if (localClose) {
				newShadeState = shadeStateClosing | shadeStateLocalAction;
			}
			else if (_shadeState & shadeStateHomeKitAction) {
				if (curTime >= _operationEndTime || _doHoldPosition) {
					SerPrintf("HomeKit operation complete.\n");
					updateState = true;
					_nextPositionUpdateTime = min(_nextPositionUpdateTime, curTime + quickUpdateTimeMS);
				}
				else {
					newShadeState = _shadeState;
				}
			}
			else {
				updateState = true;
			}

			if (updateState) {
				if (_currentShadePosition == homeKitShadeOpenValue) {
					newShadeState = shadeStateOpen;
				}
				else if (_currentShadePosition == homeKitShadeClosedValue) {
					newShadeState = shadeStateClosed;
				}
				else {
					newShadeState = shadeStateStopped;
				}
			}

			if (_holdPosition && _doHoldPosition) {
				_holdPosition->setVal(false, false);
				_doHoldPosition = false;
			}

			bool moving = (newShadeState & (shadeStateOpening | shadeStateClosing)) != 0;
			bool movingOpen = (newShadeState & shadeStateOpening) != 0;

			if (newShadeState == _shadeState) {
				if (moving) {
					_currentShadePosition += (curTime - lastTime) * ((movingOpen) ? openPerMS : -closePerMS);
					_currentShadePosition = max(homeKitShadeClosedValue, min(homeKitShadeOpenValue, _currentShadePosition));
				}
			}
			else {
				OutputState newOutputState;

				SerPrintf("State changed to 0x%02X\n", newShadeState);

				if (newShadeState & (shadeStateLocalAction | shadeStateHomeKitAction)) {
					newOutputState = (movingOpen) ? outputStateOpen : outputStateClose;
				}
				else {
					newOutputState = outputStateIdle;
				}

				if (newOutputState != _outputState) {
					SerPrintf("Output state set to %d\n", newOutputState);

					if (newOutputState == outputStateOpen) {
						digitalWrite(directionOutputPin, LOW);
						digitalWrite(controlOutputPin, HIGH);
					}
					else if (newOutputState == outputStateClose) {
						digitalWrite(directionOutputPin, HIGH);
						digitalWrite(controlOutputPin, HIGH);
					}
					else {
						digitalWrite(directionOutputPin, LOW);
						digitalWrite(controlOutputPin, LOW);
					}
					_outputState = newOutputState;
				}

				if (_nextPositionUpdateTime == 0) {
					_nextPositionUpdateTime = curTime + updateTimeMS;
				}

				_shadeState = newShadeState;
			}

			if (_nextPositionUpdateTime>0 && curTime>=_nextPositionUpdateTime) {
				SerPrintf("Position changed to %0.1f%% - output=%d\n", _currentShadePosition, _outputState);

				_currentPosition->setVal(_currentShadePosition);
				if (newShadeState & (shadeStateUserAction | shadeStateLocalAction)) {
					_targetPosition->setVal(_currentShadePosition);
				}

				uint16_t newPositionState;
				if (moving) {
					newPositionState = (movingOpen) ? homeKitPositionStateOpening : homeKitPositionStateClosing;
				}
				else {
					newPositionState = homeKitPositionStateStopped;
				}

				if (newPositionState != _positionState->getVal()) {
					SerPrintf("PositionState set to %d\n", newPositionState);

					_positionState->setVal(newPositionState);
				}

				_nextPositionUpdateTime = 0;
			}

			if (moving && _nextPositionUpdateTime == 0) {
				_nextPositionUpdateTime = curTime + updateTimeMS;
			}

			updateIndicator(_shadeState, _currentShadePosition);

			lastTime = curTime;
		}
	}
};

//////////////////////////////////////////////

RVShade* shade;

void createDevices() {
	SPAN_ACCESSORY();   // create Bridge

	SPAN_ACCESSORY(accessoryName);
		shade = new RVShade();
}

//////////////////////////////////////////////

void statusChanged(HS_STATUS status) {
	if (status == HS_WIFI_CONNECTING) {
		wifiConnected = false;
		if (hadWifiConnection) {
			SerPrintf("Lost WIFI Connection...\n");
		}
	}
}

void wifiReady() {
	wifiConnected = true;
	hadWifiConnection = true;
	SerPrintf("WIFI: Ready.\n");
}

//////////////////////////////////////////////

void setup() {
	pinMode(indicatorPowerPin, OUTPUT);
	digitalWrite(indicatorPowerPin, HIGH);

	indicator.begin();

	#ifdef DEBUG
	flashIndicator(flashColor, 20, 200);
	setIndicator(startColor);
	#endif

	SerBegin(115200);
	SerPrintf("RV-Shade Startup\n");

	SerPrintf("Init HomeSpan...\n");
	homeSpan.setSketchVersion(versionString);
	homeSpan.setWifiCallback(wifiReady);
	homeSpan.setStatusCallback(statusChanged);
	homeSpan.begin(Category::Bridges, displayName, DEFAULT_HOST_NAME, modelName);

	SerPrintf("Create devices...\n");
	createDevices();

	SerPrintf("Init complete.\n");

	SerPrintf("Wait for WiFi...\n");
	setIndicator(connectingColor);
}

void loop() {
	homeSpan.poll();

	// HomeSpan does not call the wifi callback after the first connection
	// We do that manually here
	if (hadWifiConnection && !wifiConnected) {
		static uint64_t nextWifiCheck = 0;
		uint64_t time = millis64();

		if (time >= nextWifiCheck) {
			if (WiFi.status()==WL_CONNECTED) {
				wifiReady();
			}

			nextWifiCheck = time + 500;
		}
	}
}

