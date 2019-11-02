
#include <chrono>
#include <sstream>
#include <algorithm>
#include <string.h>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <queue>
#include <memory>
#include <locale>         // std::locale, std::tolower

#include "JoyShockLibrary.h"
#include "inputHelpers.cpp"

#pragma warning(disable:4996)

// versions will be in the format A.B.C
// C increases when all that's happened is some bugs have been fixed.
// B increases and C resets to 0 when new features have been added.
// A increases and B and C reset to 0 when major new features have been added that warrant a new major version, or replacing older features with better ones that require the user to interact with them differently
const char* version = "1.3.0";

#define PI 3.14159265359f

#define MAPPING_UP 0
#define MAPPING_DOWN 1
#define MAPPING_LEFT 2
#define MAPPING_RIGHT 3
#define MAPPING_L 4
#define MAPPING_ZL 5
#define MAPPING_MINUS 6
#define MAPPING_CAPTURE 7
#define MAPPING_E 8
#define MAPPING_S 9
#define MAPPING_N 10
#define MAPPING_W 11
#define MAPPING_R 12
#define MAPPING_ZR 13
#define MAPPING_PLUS 14
#define MAPPING_HOME 15
#define MAPPING_SL 16
#define MAPPING_SR 17
#define MAPPING_L3 18
#define MAPPING_R3 19
#define MAPPING_LUP 20
#define MAPPING_LDOWN 21
#define MAPPING_LLEFT 22
#define MAPPING_LRIGHT 23
#define MAPPING_RUP 24
#define MAPPING_RDOWN 25
#define MAPPING_RLEFT 26
#define MAPPING_RRIGHT 27
#define MAPPING_ZLF 28 // FIRST
// insert more analog triggers here
#define MAPPING_ZRF 29 // LAST
#define MAPPING_SIZE 30

#define FIRST_ANALOG_TRIGGER MAPPING_ZLF
#define LAST_ANALOG_TRIGGER MAPPING_ZRF

#define MIN_GYRO_SENS 31
#define MAX_GYRO_SENS 32
#define MIN_GYRO_THRESHOLD 33
#define MAX_GYRO_THRESHOLD 34
#define STICK_POWER 35
#define STICK_SENS 36
#define REAL_WORLD_CALIBRATION 37
#define IN_GAME_SENS 38
#define TRIGGER_THRESHOLD 39
#define RESET_MAPPINGS 40
#define NO_GYRO_BUTTON 41
#define LEFT_STICK_MODE 42
#define RIGHT_STICK_MODE 43
#define GYRO_OFF 44
#define GYRO_ON 45
#define STICK_AXIS_X 46
#define STICK_AXIS_Y 47
#define GYRO_AXIS_X 48
#define GYRO_AXIS_Y 49
#define RECONNECT_CONTROLLERS 50
#define COUNTER_OS_MOUSE_SPEED 51
#define IGNORE_OS_MOUSE_SPEED 52
#define JOYCON_GYRO_MASK 53
#define GYRO_SENS 54
#define FLICK_TIME 55
#define GYRO_SMOOTH_THRESHOLD 56
#define GYRO_SMOOTH_TIME 57
#define GYRO_CUTOFF_SPEED 58
#define GYRO_CUTOFF_RECOVERY 59
#define STICK_ACCELERATION_RATE 60
#define STICK_ACCELERATION_CAP 61
#define STICK_DEADZONE_INNER 62
#define STICK_DEADZONE_OUTER 63
#define CALCULATE_REAL_WORLD_CALIBRATION 64
#define FINISH_GYRO_CALIBRATION 65
#define RESTART_GYRO_CALIBRATION 66
#define MOUSE_X_FROM_GYRO_AXIS 67
#define MOUSE_Y_FROM_GYRO_AXIS 68
#define ZR_DUAL_STAGE_MODE 69
#define ZL_DUAL_STAGE_MODE 70
#define AUTOLOAD 71

#define NO_HOLD_MAPPED 0x07
#define CALIBRATE 0x0A

#define MAGIC_DST_DELAY 150.0f // in milliseconds
#define MAGIC_HOLD_TIME 150.0f // in milliseconds
// A hold press on soft binding of a DST will add both these magic numbers

enum class StickMode { none, aim, flick, invalid };
enum class AxisMode { standard, inverted, invalid };
enum class TriggerMode { noFull, noSkip, maySkip, mustSkip, invalid };
enum class GyroAxisMask { none = 0, x = 1, y = 2, z = 4, invalid = 8 };
enum class JoyconMask { useBoth = 0, ignoreLeft = 1, ignoreRight = 2, ignoreBoth = 3, invalid = 4 };
enum class GyroIgnoreMode { none, button, left, right };
enum class DstState { NoPress = 0, PressStart, QuickSoftTap, QuickFullPress, QuickFullRelease, SoftPress, DelayFullPress, invalid };

StickMode left_stick_mode = StickMode::none;
StickMode right_stick_mode = StickMode::none;
GyroAxisMask mouse_x_from_gyro = GyroAxisMask::y;
GyroAxisMask mouse_y_from_gyro = GyroAxisMask::x;
JoyconMask joycon_gyro_mask = JoyconMask::ignoreLeft;
GyroIgnoreMode gyro_ignore_mode = GyroIgnoreMode::none;
TriggerMode zlMode = TriggerMode::noFull;
TriggerMode zrMode = TriggerMode::noFull;
WORD mappings[MAPPING_SIZE];
WORD hold_mappings[MAPPING_SIZE];
float min_gyro_sens = 0.0;
float max_gyro_sens = 0.0;
float min_gyro_threshold = 0.0;
float max_gyro_threshold = 0.0;
float stick_power = 0.0;
float stick_sens = 0.0;
int gyro_button = 0;
bool gyro_button_enables = false;
float real_world_calibration = 0.0;
float in_game_sens = 1.0;
float trigger_threshold = 0.0;
float os_mouse_speed = 1.0;
float aim_y_sign = 1.0;
float aim_x_sign = 1.0;
float gyro_y_sign = 1.0;
float gyro_x_sign = 1.0;
float flick_time = 0.0;
float gyro_smooth_time = 0.0;
float gyro_smooth_threshold = 0.0;
float gyro_cutoff_speed = 0.0;
float gyro_cutoff_recovery = 0.0;
float stick_acceleration_rate = 0.0;
float stick_acceleration_cap = 1.0;
float stick_deadzone_inner = 0.0;
float stick_deadzone_outer = 0.0;
float last_flick_and_rotation = 0.0;
std::unique_ptr<PollingThread> autoLoadThread;

char tempConfigName[128];

typedef struct ReleaseQueueItem {
	std::chrono::steady_clock::time_point time_queued;
	int index;
} ReleaseQueueItem;

typedef struct GyroSample {
	float x;
	float y;
} GyroSample;

class JoyShock {
private:
	float _weightsRemaining[16];
	float _flickSamples[16];
	int _frontSample = 0;

	GyroSample _gyroSamples[64];
	int _frontGyroSample = 0;

public:
	JoyShock(int uniqueHandle, float pollRate, int controllerSplitType, float stickStepSize)
		: intHandle(uniqueHandle)
		, poll_rate(pollRate)
		, controller_type(controllerSplitType)
		, stick_step_size(stickStepSize)
		, triggerState(LAST_ANALOG_TRIGGER - FIRST_ANALOG_TRIGGER + 1, DstState::NoPress)
	{

	}

	const int MaxGyroSamples = 64;
	const int NumSamples = 16;
	int intHandle;

	std::chrono::steady_clock::time_point press_times[MAPPING_SIZE];
	bool hold_triggered[MAPPING_SIZE];
	std::chrono::steady_clock::time_point started_flick;
	std::chrono::steady_clock::time_point time_now;
	std::queue<ReleaseQueueItem> tap_release_queue;
	float delta_flick = 0.0;
	float flick_percent_done = 0.0;
	float flick_rotation_counter = 0.0;
	bool toggleContinuous = false;

	float poll_rate;
	int controller_type = 0;
	float stick_step_size;

	float left_acceleration = 1.0;
	float right_acceleration = 1.0;
	std::vector<DstState> triggerState; // State of analog triggers when skip mode is active

	void ResetSmoothSample() {
		_frontSample = 0;
		for (int i = 0; i < NumSamples; i++) {
			_flickSamples[i] = 0.0;
		}
	}

	float GetSmoothedStickRotation(float value, float bottomThreshold, float topThreshold, int maxSamples) {
		// which sample in the circular smoothing buffer do we want to write over?
		_frontSample--;
		if (_frontSample < 0) _frontSample = NumSamples - 1;
		// if this input is bigger than the top threshold, it'll all be consumed immediately; 0 gets put into the smoothing buffer. If it's below the bottomThreshold, it'll all be put in the smoothing buffer
		float length = abs(value);
		float immediateFactor;
		if (topThreshold <= bottomThreshold) {
			immediateFactor = 0.0f;
		}
		else {
			immediateFactor = (length - bottomThreshold) / (topThreshold - bottomThreshold);
		}
		// clamp to [0, 1] range
		if (immediateFactor < 0.0f) {
			immediateFactor = 0.0f;
		}
		else if (immediateFactor > 1.0f) {
			immediateFactor = 1.0f;
		}
		float smoothFactor = 1.0f - immediateFactor;
		// now we can push the smooth sample (or as much of it as we want smoothed)
		float frontSample = _flickSamples[_frontSample] = value * smoothFactor;
		// and now calculate smoothed result
		float result = frontSample / maxSamples;
		for (int i = 1; i < maxSamples; i++) {
			int rotatedIndex = (_frontSample + i) % NumSamples;
			frontSample = _flickSamples[rotatedIndex];
			result += frontSample / maxSamples;
		}
		// finally, add immediate portion
		return result + value * immediateFactor;
	}

	void GetSmoothedGyro(float x, float y, float length, float bottomThreshold, float topThreshold, int maxSamples, float& outX, float& outY) {
		// this is basically the same as we use for smoothing flick-stick rotations, but because this deals in vectors, it's a slightly different function. Not worth abstracting until it'll be used in more ways
		// which item in the circular smoothing buffer will we write over?
		_frontGyroSample--;
		if (_frontGyroSample < 0) _frontGyroSample = MaxGyroSamples - 1;
		float immediateFactor;
		if (topThreshold <= bottomThreshold) {
			immediateFactor = length < bottomThreshold ? 0.0f : 1.0f;
		}
		else {
			immediateFactor = (length - bottomThreshold) / (topThreshold - bottomThreshold);
		}
		// clamp to [0, 1] range
		if (immediateFactor < 0.0f) {
			immediateFactor = 0.0f;
		}
		else if (immediateFactor > 1.0f) {
			immediateFactor = 1.0f;
		}
		float smoothFactor = 1.0f - immediateFactor;
		// now we can push the smooth sample (or as much of it as we want smoothed)
		GyroSample frontSample = _gyroSamples[_frontGyroSample] = { x * smoothFactor, y * smoothFactor };
		// and now calculate smoothed result
		float xResult = frontSample.x / maxSamples;
		float yResult = frontSample.y / maxSamples;
		for (int i = 1; i < maxSamples; i++) {
			int rotatedIndex = (_frontGyroSample + i) % MaxGyroSamples;
			frontSample = _gyroSamples[rotatedIndex];
			xResult += frontSample.x / maxSamples;
			yResult += frontSample.y / maxSamples;
		}
		// finally, add immediate portion
		outX = xResult + x * immediateFactor;
		outY = yResult + y * immediateFactor;
	}

	void QueueRelease(int index, std::chrono::steady_clock::time_point timeNow) {
		ReleaseQueueItem queueItem;
		queueItem.index = index;
		queueItem.time_queued = timeNow;
		tap_release_queue.push(queueItem);
	}

	~JoyShock() {
	}
};

static std::string toLower(std::string s)
{
	static std::locale loc;
	for_each(s.begin(), s.end(), [](auto c)
	{
		c = std::tolower(c, loc);
	});
	return s;
}


std::unordered_map<int, JoyShock*> handle_to_joyshock;

static int keyToBitOffset(WORD index) {
	switch (index) {
	case MAPPING_UP:
		return JSOFFSET_UP;
	case MAPPING_DOWN:
		return JSOFFSET_DOWN;
	case MAPPING_LEFT:
		return JSOFFSET_LEFT;
	case MAPPING_RIGHT:
		return JSOFFSET_RIGHT;
	case MAPPING_L:
		return JSOFFSET_L;
	case MAPPING_ZL:
		return JSOFFSET_ZL;
	case MAPPING_MINUS:
		return JSOFFSET_MINUS;
	case MAPPING_CAPTURE:
		return JSOFFSET_CAPTURE;
	case MAPPING_E:
		return JSOFFSET_E;
	case MAPPING_S:
		return JSOFFSET_S;
	case MAPPING_N:
		return JSOFFSET_N;
	case MAPPING_W:
		return JSOFFSET_W;
	case MAPPING_R:
		return JSOFFSET_R;
	case MAPPING_ZR:
		return JSOFFSET_ZR;
	case MAPPING_PLUS:
		return JSOFFSET_PLUS;
	case MAPPING_HOME:
		return JSOFFSET_HOME;
	case MAPPING_SL:
		return JSOFFSET_SL;
	case MAPPING_SR:
		return JSOFFSET_SR;
	case MAPPING_L3:
		return JSOFFSET_LCLICK;
	case MAPPING_R3:
		return JSOFFSET_RCLICK;
	}
}

/// Yes, this looks slow. But it's only there to help set up mappings more easily
static int keyToMappingIndex(std::string& s) {
	if (s.compare("UP") == 0) {
		return MAPPING_UP;
	}
	if (s.compare("DOWN") == 0) {
		return MAPPING_DOWN;
	}
	if (s.compare("LEFT") == 0) {
		return MAPPING_LEFT;
	}
	if (s.compare("RIGHT") == 0) {
		return MAPPING_RIGHT;
	}
	if (s.compare("L") == 0) {
		return MAPPING_L;
	}
	if (s.compare("ZL") == 0) {
		return MAPPING_ZL;
	}
	if (s.compare("-") == 0) {
		return MAPPING_MINUS;
	}
	if (s.compare("CAPTURE") == 0) {
		return MAPPING_CAPTURE;
	}
	if (s.compare("E") == 0) {
		return MAPPING_E;
	}
	if (s.compare("S") == 0) {
		return MAPPING_S;
	}
	if (s.compare("N") == 0) {
		return MAPPING_N;
	}
	if (s.compare("W") == 0) {
		return MAPPING_W;
	}
	if (s.compare("R") == 0) {
		return MAPPING_R;
	}
	if (s.compare("ZR") == 0) {
		return MAPPING_ZR;
	}
	if (s.compare("+") == 0) {
		return MAPPING_PLUS;
	}
	if (s.compare("HOME") == 0) {
		return MAPPING_HOME;
	}
	if (s.compare("SL") == 0) {
		return MAPPING_SL;
	}
	if (s.compare("SR") == 0) {
		return MAPPING_SR;
	}
	if (s.compare("L3") == 0) {
		return MAPPING_L3;
	}
	if (s.compare("R3") == 0) {
		return MAPPING_R3;
	}
	if (s.compare("LUP") == 0) {
		return MAPPING_LUP;
	}
	if (s.compare("LDOWN") == 0) {
		return MAPPING_LDOWN;
	}
	if (s.compare("LLEFT") == 0) {
		return MAPPING_LLEFT;
	}
	if (s.compare("LRIGHT") == 0) {
		return MAPPING_LRIGHT;
	}
	if (s.compare("RUP") == 0) {
		return MAPPING_RUP;
	}
	if (s.compare("RDOWN") == 0) {
		return MAPPING_RDOWN;
	}
	if (s.compare("RLEFT") == 0) {
		return MAPPING_RLEFT;
	}
	if (s.compare("RRIGHT") == 0) {
		return MAPPING_RRIGHT;
	}
	if (s.compare("ZRF") == 0) {
		return MAPPING_ZRF;
	}
	if (s.compare("ZLF") == 0) {
		return MAPPING_ZLF;
	}
	if (s.compare("MIN_GYRO_SENS") == 0) {
		return MIN_GYRO_SENS;
	}
	if (s.compare("MAX_GYRO_SENS") == 0) {
		return MAX_GYRO_SENS;
	}
	if (s.compare("MIN_GYRO_THRESHOLD") == 0) {
		return MIN_GYRO_THRESHOLD;
	}
	if (s.compare("MAX_GYRO_THRESHOLD") == 0) {
		return MAX_GYRO_THRESHOLD;
	}
	if (s.compare("STICK_POWER") == 0) {
		return STICK_POWER;
	}
	if (s.compare("STICK_SENS") == 0) {
		return STICK_SENS;
	}
	if (s.compare("REAL_WORLD_CALIBRATION") == 0) {
		return REAL_WORLD_CALIBRATION;
	}
	if (s.compare("IN_GAME_SENS") == 0) {
		return IN_GAME_SENS;
	}
	if (s.compare("TRIGGER_THRESHOLD") == 0) {
		return TRIGGER_THRESHOLD;
	}
	if (s.compare("RESET_MAPPINGS") == 0) {
		return RESET_MAPPINGS;
	}
	if (s.compare("NO_GYRO_BUTTON") == 0) {
		return NO_GYRO_BUTTON;
	}
	if (s.compare("LEFT_STICK_MODE") == 0) {
		return LEFT_STICK_MODE;
	}
	if (s.compare("RIGHT_STICK_MODE") == 0) {
		return RIGHT_STICK_MODE;
	}
	if (s.compare("GYRO_OFF") == 0) {
		return GYRO_OFF;
	}
	if (s.compare("GYRO_ON") == 0) {
		return GYRO_ON;
	}
	if (s.compare("STICK_AXIS_X") == 0) {
		return STICK_AXIS_X;
	}
	if (s.compare("STICK_AXIS_Y") == 0) {
		return STICK_AXIS_Y;
	}
	if (s.compare("GYRO_AXIS_X") == 0) {
		return GYRO_AXIS_X;
	}
	if (s.compare("GYRO_AXIS_Y") == 0) {
		return GYRO_AXIS_Y;
	}
	if (s.compare("RECONNECT_CONTROLLERS") == 0) {
		return RECONNECT_CONTROLLERS;
	}
	if (s.compare("COUNTER_OS_MOUSE_SPEED") == 0) {
		return COUNTER_OS_MOUSE_SPEED;
	}
	if (s.compare("IGNORE_OS_MOUSE_SPEED") == 0) {
		return IGNORE_OS_MOUSE_SPEED;
	}
	if (s.compare("JOYCON_GYRO_MASK") == 0) {
		return JOYCON_GYRO_MASK;
	}
	if (s.compare("GYRO_SENS") == 0) {
		return GYRO_SENS;
	}
	if (s.compare("FLICK_TIME") == 0) {
		return FLICK_TIME;
	}
	if (s.compare("GYRO_SMOOTH_THRESHOLD") == 0) {
		return GYRO_SMOOTH_THRESHOLD;
	}
	if (s.compare("GYRO_SMOOTH_TIME") == 0) {
		return GYRO_SMOOTH_TIME;
	}
	if (s.compare("GYRO_CUTOFF_SPEED") == 0) {
		return GYRO_CUTOFF_SPEED;
	}
	if (s.compare("GYRO_CUTOFF_RECOVERY") == 0) {
		return GYRO_CUTOFF_RECOVERY;
	}
	if (s.compare("STICK_ACCELERATION_RATE") == 0) {
		return STICK_ACCELERATION_RATE;
	}
	if (s.compare("STICK_ACCELERATION_CAP") == 0) {
		return STICK_ACCELERATION_CAP;
	}
	if (s.compare("STICK_DEADZONE_INNER") == 0) {
		return STICK_DEADZONE_INNER;
	}
	if (s.compare("STICK_DEADZONE_OUTER") == 0) {
		return STICK_DEADZONE_OUTER;
	}
	if (s.rfind("CALCULATE_REAL_WORLD_CALIBRATION", 0) == 0) {
		return CALCULATE_REAL_WORLD_CALIBRATION;
	}
	if (s.rfind("FINISH_GYRO_CALIBRATION", 0) == 0) {
		return FINISH_GYRO_CALIBRATION;
	}
	if (s.rfind("RESTART_GYRO_CALIBRATION", 0) == 0) {
		return RESTART_GYRO_CALIBRATION;
	}
	if (s.rfind("MOUSE_X_FROM_GYRO_AXIS", 0) == 0) {
		return MOUSE_X_FROM_GYRO_AXIS;
	}
	if (s.rfind("MOUSE_Y_FROM_GYRO_AXIS", 0) == 0) {
		return MOUSE_Y_FROM_GYRO_AXIS;
	}
	if (s.rfind("ZR_MODE", 0) == 0) {
		return ZR_DUAL_STAGE_MODE;
	}
	if (s.rfind("ZL_MODE", 0) == 0) {
		return ZL_DUAL_STAGE_MODE;
	}
	if (s.rfind("AUTOLOAD", 0) == 0) {
		return AUTOLOAD;
	}
	return -1;
}

static StickMode nameToStickMode(std::string& name, bool print = false) {
	if (name.compare("AIM") == 0) {
		if (print) printf("Aim");
		return StickMode::aim;
	}
	if (name.compare("FLICK") == 0) {
		if (print) printf("Flick");
		return StickMode::flick;
	}
	if (name.compare("NO_MOUSE") == 0) {
		if (print) printf("No mouse");
		return StickMode::none;
	}
	if (print) printf("\"%s\" invalid", name.c_str());
	return StickMode::invalid;
}

static AxisMode nameToAxisMode(std::string& name, bool print = false) {
	if (name.compare("STANDARD") == 0) {
		if (print) printf("Standard");
		return AxisMode::standard;
	}
	if (name.compare("INVERTED") == 0) {
		if (print) printf("Inverted");
		return AxisMode::inverted;
	}
	if (print) printf("\"%s\" invalid", name.c_str());
	return AxisMode::invalid;
}

static TriggerMode nameToTriggerMode(std::string& name, bool print = false) {
	if (name.compare("NO_FULL") == 0) {
		if (print) printf("Trigger will never apply full pull binding");
		return TriggerMode::noFull;
	}
	if (name.compare("NO_SKIP") == 0) {
		if (print) printf("Trigger will never skip trigger binding to apply full pull binding");
		return TriggerMode::noSkip;
	}
	if (name.compare("MAY_SKIP") == 0) {
		if (print) printf("Trigger can skip trigger binding on a quick full pull");
		return TriggerMode::maySkip;
	}
	if (name.compare("MUST_SKIP") == 0) {
		if (print) printf("Trigger can only apply full pull binding on a quick full pull, skipping trigger binding");
		return TriggerMode::mustSkip;
	}
	if (print) printf("\"%s\" invalid", name.c_str());
	return TriggerMode::invalid;
}

static GyroAxisMask nameToGyroAxisMask(std::string& name, bool print = false) {
	if (name.compare("X") == 0) {
		if (print) printf("X");
		return GyroAxisMask::x;
	}
	if (name.compare("Y") == 0) {
		if (print) printf("Y");
		return GyroAxisMask::y;
	}
	if (name.compare("Z") == 0) {
		if (print) printf("Z");
		return GyroAxisMask::z;
	}
	if (name.compare("NONE") == 0) {
		if (print) printf("NONE");
		return GyroAxisMask::none;
	}
	if (print) printf("\"%s\" invalid", name.c_str());
	return GyroAxisMask::invalid;
}

static JoyconMask nameToJoyconMask(std::string& name, bool print = false) {
	if (name.compare("USE_BOTH") == 0) {
		if (print) printf("Use both");
		return JoyconMask::useBoth;
	}
	if (name.compare("IGNORE_LEFT") == 0) {
		if (print) printf("Ignore left");
		return JoyconMask::ignoreLeft;
	}
	if (name.compare("IGNORE_RIGHT") == 0) {
		if (print) printf("Ignore right");
		return JoyconMask::ignoreRight;
	}
	if (name.compare("IGNORE_BOTH") == 0) {
		if (print) printf("Ignore both");
		return JoyconMask::ignoreBoth;
	}
	if (print) printf("\"%s\" invalid", name.c_str());
	return JoyconMask::invalid;
}

// https://stackoverflow.com/questions/25345598/c-implementation-to-trim-char-array-of-leading-trailing-white-space-not-workin
static void strtrim(char* str) {
	int start = 0; // number of leading spaces
	char* buffer = str;
	while (*str && *str++ == ' ') ++start;
	while (*str++); // move to end of string
	int end = str - buffer - 1;
	while (end > 0 && buffer[end - 1] == ' ') --end; // backup over trailing spaces
	buffer[end] = 0; // remove trailing spaces
	if (end <= start || start == 0) return; // exit if no leading spaces or string is now empty
	str = buffer + start;
	while ((*buffer++ = *str++));  // remove leading spaces: K&R
}

static void resetAllMappings() {
	memset(mappings, 0, sizeof(mappings));
	memset(hold_mappings, 0, sizeof(hold_mappings));
	mappings[MAPPING_HOME] = mappings[MAPPING_CAPTURE] = hold_mappings[MAPPING_HOME] = hold_mappings[MAPPING_CAPTURE] = CALIBRATE;
	min_gyro_sens = 0.0f;
	max_gyro_sens = 0.0f;
	min_gyro_threshold = 0.0f;
	max_gyro_threshold = 0.0f;
	stick_power = 1.0f;
	stick_sens = 360.0f;
	real_world_calibration = 40.0f;
	in_game_sens = 1.0f;
	left_stick_mode = StickMode::none;
	right_stick_mode = StickMode::none;
	mouse_x_from_gyro = GyroAxisMask::y;
	mouse_y_from_gyro = GyroAxisMask::x;
	joycon_gyro_mask = JoyconMask::ignoreLeft;
	zlMode = TriggerMode::noFull;
	zrMode = TriggerMode::noFull;
	trigger_threshold = 0.0f;
	os_mouse_speed = 1.0f;
	gyro_button = 0;
	gyro_button_enables = false;
	aim_y_sign = 1.0f;
	aim_x_sign = 1.0f;
	gyro_y_sign = 1.0f;
	gyro_x_sign = 1.0f;
	flick_time = 0.1f;
	gyro_smooth_time = 0.125f;
	gyro_smooth_threshold = 0.0f;
	gyro_cutoff_speed = 0.0f;
	gyro_cutoff_recovery = 0.0f;
	stick_acceleration_rate = 0.0f;
	stick_acceleration_cap = 1000000.0f;
	stick_deadzone_inner = 0.15f;
	stick_deadzone_outer = 0.1f;
	last_flick_and_rotation = 0.0f;
}

void joyShockPollCallback(int jcHandle, JOY_SHOCK_STATE state, JOY_SHOCK_STATE lastState, IMU_STATE imuState, IMU_STATE lastImuState, float deltaTime);
void connectDevices();
static void parseCommand(std::string line);

static bool loadMappings(std::string fileName) {
	// https://stackoverflow.com/questions/2602013/read-whole-ascii-file-into-c-stdstring
	std::ifstream t(fileName);
	if (t)
	{
		printf("Loading commands from file %s\n", fileName.c_str());
		// https://stackoverflow.com/questions/6892754/creating-a-simple-configuration-file-and-parser-in-c
		std::string line;
		while (getline(t, line)) {
			parseCommand(line);
		}
		t.close();
		return true;
	}
	else {
		return false;
	}
}

static void parseCommand(std::string line) {
	if (line[0] == 0) {
		return;
	}
	// # for comments
	if (line[0] == '#') {
		return;
	}
	// replace further comment markers with null so that input ends there
	std::replace(line.begin(), line.end(), '#', (char)0);
	if (loadMappings(line)) {
		return;
	}
	//printf("Processing line %s\n", line);
	std::istringstream is_line(line);
	char key[128];
	if (is_line.getline(key, 128, '=')) {
		strtrim(key);
		//printf("Key: %s; ", key);
		int index = keyToMappingIndex(std::string(key));
		// unary commands
		switch (index) {
		case NO_GYRO_BUTTON:
			printf("No button disables or enables gyro\n");
			gyro_button = 0;
			gyro_button_enables = false;
			gyro_ignore_mode = GyroIgnoreMode::none;
			return;
		case RESET_MAPPINGS:
			printf("Resetting all mappings to defaults\n");
			resetAllMappings();
			return;
		case RECONNECT_CONTROLLERS:
			printf("Reconnecting controllers\n");
			JslDisconnectAndDisposeAll();
			connectDevices();
			JslSetCallback(&joyShockPollCallback);
			return;
		case COUNTER_OS_MOUSE_SPEED:
			printf("Countering OS mouse speed setting\n");
			os_mouse_speed = getMouseSpeed();
			return;
		case IGNORE_OS_MOUSE_SPEED:
			printf("Ignoring OS mouse speed setting\n");
			os_mouse_speed = 1.0;
			return;
		case CALCULATE_REAL_WORLD_CALIBRATION: {
			// first, check for a parameter
			std::string keyAsString = std::string(key);
			std::string crwcArgument = keyAsString.substr(std::string("CALCULATE_REAL_WORLD_CALIBRATION").length(), 64); // no argument bigger than 64, I assume
			float numRotations = 1.0;
			if (crwcArgument.length() > 0) {
				try {
					numRotations = std::stof(crwcArgument);
				}
				catch (std::invalid_argument ia) {
					printf("Can't convert \"%s\" to a number\n", crwcArgument.c_str());
					return;
				}
			}
			if (numRotations == 0) {
				printf("Can't calculate calibration from zero rotations\n");
			}
			else if (last_flick_and_rotation == 0) {
				printf("Need to use the flick stick at least once before calculating an appropriate calibration value\n");
			}
			else {
				printf("Recommendation: REAL_WORLD_CALIBRATION = %.5g\n", real_world_calibration * last_flick_and_rotation / numRotations);
			}
			return;
		}
		case FINISH_GYRO_CALIBRATION: {
			printf("Finishing continuous calibration for all devices\n");
			for (std::unordered_map<int, JoyShock*>::iterator iter = handle_to_joyshock.begin(); iter != handle_to_joyshock.end(); ++iter) {
				JoyShock* jc = iter->second;
				JslPauseContinuousCalibration(jc->intHandle);
			}
			return;
		}
		case RESTART_GYRO_CALIBRATION: {
			printf("Restarting continuous calibration for all devices\n");
			for (std::unordered_map<int, JoyShock*>::iterator iter = handle_to_joyshock.begin(); iter != handle_to_joyshock.end(); ++iter) {
				JoyShock* jc = iter->second;
				JslResetContinuousCalibration(jc->intHandle);
				JslStartContinuousCalibration(jc->intHandle);
			}
			return;
		}
		}
		char value[128];
		// other commands
		if (is_line.getline(value, 128)) {
			strtrim(value);
			//printf("Value: %s;\n", value);
			// bam! got our thingy!
			// now, let's map!
			if (index < 0) {
				printf("Warning: Input %s not recognised\n", key);
				return;
			}
			else if (index == MAPPING_SIZE) {
				printf("Error: Input %s somehow mapped to array size\n", key);
				return;
			}
			// settings
			else if (index > MAPPING_SIZE) {
				switch (index) {
				case MIN_GYRO_SENS:
					try {
						min_gyro_sens = std::stof(value);
						printf("Min gyro sensitivity set to %sx\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				case MAX_GYRO_SENS:
					try {
						max_gyro_sens = std::stof(value);
						printf("Max gyro sensitivity set to %sx\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				case MIN_GYRO_THRESHOLD:
					try {
						min_gyro_threshold = std::stof(value);
						printf("Min gyro sensitivity used at and below %sdps\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				case MAX_GYRO_THRESHOLD:
					try {
						max_gyro_threshold = std::stof(value);
						printf("Max gyro sensitivity used at and above %sdps\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				case STICK_POWER:
					try {
						stick_power = max(0.0f, std::stof(value));
						printf("Stick power set to %s\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				case STICK_SENS:
					try {
						stick_sens = std::stof(value);
						printf("Stick sensitivity set to %sdps\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				case REAL_WORLD_CALIBRATION:
					try {
						real_world_calibration = std::stof(value);
						printf("Real world calibration set to %s\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				case IN_GAME_SENS:
				{
					try {
						float old_sens = in_game_sens;
						in_game_sens = std::stof(value);
						if (in_game_sens == 0) {
							in_game_sens = old_sens;
							printf("Can't set IN_GAME_SENS to zero. Reverting to previous setting (%0.4f)\n", in_game_sens);
						}
						else {
							printf("In game sensitivity set to %s\n", value);
						}
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				}
				case TRIGGER_THRESHOLD:
					try {
						trigger_threshold = std::stof(value);
						printf("Trigger ignored below %s\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				case LEFT_STICK_MODE:
				{
					printf("Left stick: ");
					StickMode temp = nameToStickMode(std::string(value), true);
					printf("\n");
					if (temp != StickMode::invalid) {
						left_stick_mode = temp;
					}
					else {
						printf("Valid settings for LEFT_STICK_MODE are NO_MOUSE, AIM, or FLICK\n");
					}
					return;
				}
				case RIGHT_STICK_MODE:
				{
					printf("Right stick: ");
					StickMode temp = nameToStickMode(std::string(value), true);
					printf("\n");
					if (temp != StickMode::invalid) {
						right_stick_mode = temp;
					}
					else {
						printf("Valid settings for RIGHT_STICK_MODE are NO_MOUSE, AIM, or FLICK\n");
					}
					return;
				}
				case STICK_AXIS_X:
				{
					printf("Stick aim X axis: ");
					AxisMode temp = nameToAxisMode(std::string(value), true);
					printf("\n");
					if (temp != AxisMode::invalid) {
						aim_x_sign = temp == AxisMode::standard ? 1.0f : -1.0f;
					}
					else {
						printf("Valid settings for STICK_AXIS_X are STANDARD or INVERTED\n");
					}
					return;
				}
				case STICK_AXIS_Y:
				{
					printf("Stick aim Y axis: ");
					AxisMode temp = nameToAxisMode(std::string(value), true);
					printf("\n");
					if (temp != AxisMode::invalid) {
						aim_y_sign = temp == AxisMode::standard ? 1.0f : -1.0f;
					}
					else {
						printf("Valid settings for STICK_AXIS_Y are STANDARD or INVERTED\n");
					}
					return;
				}
				case GYRO_AXIS_X:
				{
					printf("Gyro aim X axis: ");
					AxisMode temp = nameToAxisMode(std::string(value), true);
					printf("\n");
					if (temp != AxisMode::invalid) {
						gyro_x_sign = temp == AxisMode::standard ? 1.0f : -1.0f;
					}
					else {
						printf("Valid settings for GYRO_AXIS_X are STANDARD or INVERTED\n");
					}
					return;
				}
				case GYRO_AXIS_Y:
				{
					printf("Gyro aim Y axis: ");
					AxisMode temp = nameToAxisMode(std::string(value), true);
					printf("\n");
					if (temp != AxisMode::invalid) {
						gyro_y_sign = temp == AxisMode::standard ? 1.0f : -1.0f;
					}
					else {
						printf("Valid settings for GYRO_AXIS_Y are STANDARD or INVERTED\n");
					}
					return;
				}
				case JOYCON_GYRO_MASK:
				{
					printf("Joycon gyro mask: ");
					JoyconMask temp = nameToJoyconMask(std::string(value), true);
					printf("\n");
					if (temp != JoyconMask::invalid) {
						joycon_gyro_mask = temp;
						printf("Joycon gyro mask set to %s\n", value);
					}
					else {
						printf("Valid settings for JOYCON_GYRO_MASK are IGNORE_LEFT, IGNORE_RIGHT, IGNORE_BOTH, or USE_BOTH\n");
					}
					return;
				}
				case MOUSE_X_FROM_GYRO_AXIS:
				{
					printf("Mouse x reads gyro axis: ");
					GyroAxisMask temp = nameToGyroAxisMask(std::string(value), true);
					printf("\n");
					if (temp != GyroAxisMask::invalid) {
						mouse_x_from_gyro = temp;
						if (temp == GyroAxisMask::none) {
							printf("Gyro not used for mouse x axis\no");
						}
						else {
							printf("Mouse x axis set to gyro %s axis\n", value);
						}
					}
					else {
						printf("Valid settings for MOUSE_X_FROM_GYRO_AXIS are X, Y, Z, or NONE\n");
					}
					return;
				}
				case MOUSE_Y_FROM_GYRO_AXIS:
				{
					printf("Mouse y reads gyro axis: ");
					GyroAxisMask temp = nameToGyroAxisMask(std::string(value), true);
					printf("\n");
					if (temp != GyroAxisMask::invalid) {
						mouse_y_from_gyro = temp;
						if (temp == GyroAxisMask::none) {
							printf("Gyro not used for mouse y axis\n");
						}
						else {
							printf("Mouse y axis set to gyro %s axis\n", value);
						}
					}
					else {
						printf("Valid settings for MOUSE_Y_FROM_GYRO_AXIS are X, Y, Z, or NONE\n");
					}
					return;
				}
				case GYRO_OFF:
				{
					std::string valueName = std::string(value);
					int rhsMappingIndex = keyToMappingIndex(valueName);
					if (rhsMappingIndex >= 0 && rhsMappingIndex < MAPPING_SIZE &&
						rhsMappingIndex != MAPPING_ZRF && rhsMappingIndex != MAPPING_ZLF) // Full triggers are not valid options for gyro control
					{
						gyro_button_enables = false;
						gyro_ignore_mode = GyroIgnoreMode::button;
						gyro_button = 1 << keyToBitOffset(rhsMappingIndex);
						printf("Disable gyro with %s\n", value);
					}
					else if (valueName.compare("LEFT_STICK") == 0)
					{
						gyro_button_enables = false;
						gyro_ignore_mode = GyroIgnoreMode::left;
						printf("Disable gyro when left stick is used\n");
					}
					else if (valueName.compare("RIGHT_STICK") == 0)
					{
						gyro_button_enables = false;
						gyro_ignore_mode = GyroIgnoreMode::right;
						printf("Disable gyro when right stick is used\n");
					}
					else {
						printf("Can't map GYRO_OFF to \"%s\". Gyro button can be unmapped with \"NO_GYRO_BUTTON\"\n", value);
					}
					return;
				}
				case GYRO_ON:
				{
					std::string valueName = std::string(value);
					int rhsMappingIndex = keyToMappingIndex(valueName);
					if (rhsMappingIndex >= 0 && rhsMappingIndex < MAPPING_SIZE&&
						rhsMappingIndex != MAPPING_ZRF && rhsMappingIndex != MAPPING_ZLF) // Full triggers are not valid options for gyro control
					{
						gyro_button_enables = true;
						gyro_ignore_mode = GyroIgnoreMode::button;
						gyro_button = 1 << keyToBitOffset(rhsMappingIndex);
						printf("Enable gyro with %s\n", value);
					}
					else if (valueName.compare("LEFT_STICK") == 0)
					{
						gyro_button_enables = true;
						gyro_ignore_mode = GyroIgnoreMode::left;
						printf("Enable gyro when left stick is used\n");
					}
					else if (valueName.compare("RIGHT_STICK") == 0)
					{
						gyro_button_enables = true;
						gyro_ignore_mode = GyroIgnoreMode::right;
						printf("Enable gyro when right stick is used\n");
					}
					else {
						printf("Can't map GYRO_ON to \"%s\". Gyro button can be unmapped with \"NO_GYRO_BUTTON\"\n", value);
					}
					return;
				}
				case GYRO_SENS:
				{
					try {
						float sens = std::stof(value);
						min_gyro_sens = max_gyro_sens = sens;
						printf("Gyro sensitivity set to %sx\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				}
				case FLICK_TIME:
				{
					try {
						float val = std::stof(value);
						if (val < 0.0001f) {
							val = 0.0001f; // prevent it being actually zero
						}
						flick_time = val;
						printf("Flick time set to %ss\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				}
				case GYRO_SMOOTH_THRESHOLD:
				{
					try {
						gyro_smooth_threshold = std::stof(value);
						printf("Gyro smoothing ignored at and above %sdps\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				}
				case GYRO_SMOOTH_TIME:
				{
					try {
						gyro_smooth_time = std::stof(value);
						printf("GYRO_SMOOTH_TIME set to %ss\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				}
				case GYRO_CUTOFF_SPEED:
				{
					try {
						gyro_cutoff_speed = std::stof(value);
						printf("Ignoring gyro speed below %sdps\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				}
				case GYRO_CUTOFF_RECOVERY:
				{
					try {
						gyro_cutoff_recovery = std::stof(value);
						printf("Gyro cutoff recovery set to %sdps\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				}
				case STICK_ACCELERATION_RATE:
				{
					try {
						stick_acceleration_rate = max(0.0f, std::stof(value));
						printf("Stick speed when fully pressed increases by a factor of %s per second\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				}
				case STICK_ACCELERATION_CAP:
				{
					try {
						stick_acceleration_cap = max(1.0f, std::stof(value));
						printf("Stick acceleration factor capped at %s\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				}
				case STICK_DEADZONE_INNER:
				{
					try {
						stick_deadzone_inner = max(0.0f, min(1.0f, std::stof(value)));
						printf("Stick treats any value within %s of the centre as the centre\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				}
				case STICK_DEADZONE_OUTER:
				{
					try {
						stick_deadzone_outer = max(0.0f, min(1.0f, std::stof(value)));
						printf("Stick treats any value within %s of the edge as the edge\n", value);
					}
					catch (std::invalid_argument ia) {
						printf("Can't convert \"%s\" to a number\n", value);
					}
					return;
				}
				case ZR_DUAL_STAGE_MODE:
				{
					printf("Right trigger: ");
					TriggerMode temp = nameToTriggerMode(std::string(value), true);
					printf("\n");
					if (temp != TriggerMode::invalid) {
						zrMode = temp;
						for (auto js : handle_to_joyshock)
						{
							if (JslGetControllerType(js.first) != JS_TYPE_DS4)
							{
								printf("WARNING: Dual Stage Triggers are only valid on analog triggers. Full pull bindings will be ignored on non DS4 controllers.\n");
							}
						}
					}
					else {
						printf("Valid settings for ZR_MODE are NO_FULL, NO_SKIP, MAY_SKIP and MUST_SKIP\n");
					}
					return;
				}
				case ZL_DUAL_STAGE_MODE:
				{
					printf("Left trigger: ");
					TriggerMode temp = nameToTriggerMode(std::string(value), true);
					printf("\n");
					if (temp != TriggerMode::invalid) {
						zlMode = temp;
						for (auto js : handle_to_joyshock)
						{
							if (JslGetControllerType(js.first) != JS_TYPE_DS4)
							{
								printf("WARNING: Dual Stage Triggers are only valid on analog triggers. Full pull bindings will be ignored on non DS4 controllers.\n");
							}
						}
					}
					else {
						printf("Valid settings for ZL_MODE are NO_FULL, NO_SKIP, MAY_SKIP and MUST_SKIP\n");
					}
					return;
				}
				case AUTOLOAD:
				{
					if (!autoLoadThread)
					{
						printf("AutoLoad is unavailable\n");
						return;
					}
					std::string valueName(value);
					if (valueName.compare("ON") == 0)
					{
						autoLoadThread->Start();
						printf("Autoload is now running.\n");
					}
					else if (valueName.compare("OFF") == 0)
					{
						autoLoadThread->Stop();
						printf("Autoload is stopped\n");
					}
					else {
						printf("%s is an invalid option\nAUTOLOAD can only be assigned ON or OFF\n", value);
					}
					return;
				}
				default:
					printf("Error: Input %s somehow mapped to value out of mapping range\n", key);
				}
			}
			// we have a valid input, so clear old hold_mapping for this input
			hold_mappings[index] = 0;
			WORD output = nameToKey(std::string(value));
			if (output == NO_HOLD_MAPPED) {
				output = 0;
				printf("%s mapped to no input\n", key);
			}
			else if (output == 0x00) {
				// try splitting -- it might be a tap and hold
				char* subValue = value;
				while (*subValue && *++subValue != ' '); // find a space
				if (*subValue) {
					// we haven't reached the end, we're good to go
					*subValue = 0; // null terminating. now we maybe have two strings
				}
				subValue++;
				output = nameToKey(std::string(value));
				WORD holdOutput = nameToKey(std::string(subValue));
				if (output == 0x00) {
					// tap and hold requires the tap to be a regular input. Hold can be none, if only taps should register input
					printf("Tap output %s for %s not recognised\n", value, key);
				}
				else {
					printf("Tap %s mapped to %s\n", key, value);
				}
				if (holdOutput == 0x00) {
					printf("Hold output %s for input %s not recognised\n", subValue, key);
				}
				else {
					hold_mappings[index] = holdOutput;
					printf("Hold %s mapped to %s\n", key, subValue);
				}

				// translate a "NONE" to 0 -- only keep it as a NO_HOLD_MAPPING for hold_mappings
				if (output == NO_HOLD_MAPPED) {
					output = 0;
				}
			}
			else {
				printf("%s mapped to %s\n", key, value);
			}
			mappings[index] = output;
		}
		else {
			printf("Command \"%s\" not recognised\n", line.c_str());
		}
	}
}

// https://stackoverflow.com/questions/25144887/map-unordered-map-prefer-find-and-then-at-or-try-at-catch-out-of-range
JoyShock* getJoyShockFromHandle(int handle) {
	auto iter = handle_to_joyshock.find(handle);

	if (iter != handle_to_joyshock.end()) {
		return iter->second;
		// iter is item pair in the map. The value will be accessible as `iter->second`.
	}
	return nullptr;
}

// return true if it hits the outer deadzone
bool processDeadZones(float& x, float& y) {
	float innerDeadzone = stick_deadzone_inner;
	float outerDeadzone = 1.0f - stick_deadzone_outer;
	float length = sqrtf(x*x + y * y);
	if (length <= innerDeadzone) {
		x = 0.0f;
		y = 0.0f;
		return false;
	}
	if (length >= outerDeadzone) {
		// normalize
		x /= length;
		y /= length;
		return true;
	}
	if (length > innerDeadzone) {
		float scaledLength = (length - innerDeadzone) / (outerDeadzone - innerDeadzone);
		float rescale = scaledLength / length;
		x *= rescale;
		y *= rescale;
	}
	return false;
}

void handleButtonChange(int index, bool lastPressed, bool pressed, const char* name, JoyShock* jc) {
	if (pressed && lastPressed && !jc->hold_triggered[index]) {
		// does it need hold-checking?
		if (hold_mappings[index] != 0 && hold_mappings[index] != NO_HOLD_MAPPED) {
			// it does!
			float pressTimeMS = ((float)std::chrono::duration_cast<std::chrono::microseconds>(jc->time_now - jc->press_times[index]).count()) / 1000.0f;
			if (pressTimeMS >= MAGIC_HOLD_TIME) { // todo: get rid of magic number -- make this a user setting
				pressKey(hold_mappings[index], true); // start pressing the hold key!
				jc->hold_triggered[index] = true;
				printf("%s: held\n", name);
				// reset calibration
				if (hold_mappings[index] == CALIBRATE) {
					printf("Resetting continuous calibration\n");
					JslResetContinuousCalibration(jc->intHandle);
					JslStartContinuousCalibration(jc->intHandle);
				}
			}
		}
	}
	if (lastPressed != pressed) {
		if (hold_mappings[index] != 0 && hold_mappings[index] != NO_HOLD_MAPPED) {
			// we have to handle tapping or holding for this button!
			if (pressed) {
				// started being pressed, so start tracking time for this input
				jc->press_times[index] = jc->time_now;
			}
			else {
				// released, so we have to either queue up a tap action or just do a simple release of the mapped hold key
				float pressTimeMS = ((float)std::chrono::duration_cast<std::chrono::microseconds>(jc->time_now - jc->press_times[index]).count()) / 1000.0f;
				if (pressTimeMS >= MAGIC_HOLD_TIME) { // todo: get rid of magic number -- make this a user setting
					// it was a hold!
					pressKey(hold_mappings[index], false);
					jc->hold_triggered[index] = false;
					printf("%s: hold released\n", name);
					// handle calibration offset
					if (hold_mappings[index] == CALIBRATE) {
						JslPauseContinuousCalibration(jc->intHandle);
						jc->toggleContinuous = false; // if we've held the calibration button, we're disabling continuous calibration
						printf("Gyro calibration set\n");
					}
				}
				else {
					// it was a tap! press and then queue up a release
					pressKey(mappings[index], true);
					jc->QueueRelease(index, jc->time_now + std::chrono::milliseconds(30)); // another magic number
					jc->hold_triggered[index] = false;
					printf("%s: tapped\n", name);
					// handle continous calibration toggle
					if (mappings[index] == CALIBRATE) {
						jc->toggleContinuous = !jc->toggleContinuous;
						if (jc->toggleContinuous) {
							JslResetContinuousCalibration(jc->intHandle);
							JslStartContinuousCalibration(jc->intHandle);
							printf("Enabled continuous calibration\n");
						}
						else {
							JslPauseContinuousCalibration(jc->intHandle);
							printf("Disabled continuous calibration\n");
						}
					}
				}
			}
		}
		else {
			pressKey(mappings[index], pressed);
			jc->hold_triggered[index] = false;
			printf("%s: %s\n", name, pressed ? "true" : "false");
			if (mappings[index] == CALIBRATE) {
				if (pressed) {
					printf("Resetting continuous calibration\n");
					JslResetContinuousCalibration(jc->intHandle);
					JslStartContinuousCalibration(jc->intHandle);
				}
				else {
					JslPauseContinuousCalibration(jc->intHandle);
					jc->toggleContinuous = false; // if we've held the calibration button, we're disabling continuous calibration
					printf("Gyro calibration set\n");
				}
			}
		}
	}
}

void handleTriggerChange(int softIndex, int fullIndex, TriggerMode mode, float lastPressed, float pressed, char* softName, JoyShock* jc) {
	std::string fullName(softName);
	fullName.append("F");

	if (JslGetControllerType(jc->intHandle) != JS_TYPE_DS4)
	{
		// Override local variable because the controller has digital triggers. Effectively ignore Full Pull binding.
		mode = TriggerMode::noFull;
	}

	auto idxState = fullIndex - FIRST_ANALOG_TRIGGER; // Get analog trigger index
	if (idxState < 0 || idxState >= jc->triggerState.size())
	{
		printf("Error: Trigger %s does not exist in state map. Dual Stage Trigger not possible.\n", fullName);
		return;
	}
	switch (jc->triggerState[idxState])
	{
	case DstState::NoPress:
		// It actually doesn't matter what the last Press is. Theoretically, we could have missed the edge.
		if (pressed > trigger_threshold)
		{
			if (mode == TriggerMode::maySkip || mode == TriggerMode::mustSkip)
			{
				// Start counting press time to see if soft binding should be skipped
				jc->triggerState[idxState] = DstState::PressStart;
				jc->press_times[softIndex] = jc->time_now;
			}
			else // mode == NO_FULL or NO_SKIP
			{
				jc->triggerState[idxState] = DstState::SoftPress;
				// Reset the time for hold soft press purposes.
				jc->press_times[softIndex] = jc->time_now;
				handleButtonChange(softIndex, false, true, softName, jc);
			}
		}
		break;
	case DstState::PressStart:
		if (lastPressed > trigger_threshold && pressed <= trigger_threshold) {
			// Trigger has been quickly tapped on the soft press
			jc->triggerState[idxState] = DstState::QuickSoftTap;
			handleButtonChange(softIndex, false, true, softName, jc);
		}
		else if (lastPressed > trigger_threshold && lastPressed < 1.0 && pressed == 1.0)
		{
			// Trigger has been full pressed quickly
			jc->triggerState[idxState] = DstState::QuickFullPress;
			handleButtonChange(fullIndex, false, true, fullName.c_str(), jc);
		}
		else
		{
			float pressTimeMS = ((float)std::chrono::duration_cast<std::chrono::microseconds>(jc->time_now - jc->press_times[softIndex]).count()) / 1000.0f;
			if (pressTimeMS >= MAGIC_DST_DELAY) { // todo: get rid of magic number -- make this a user setting )
				jc->triggerState[idxState] = DstState::SoftPress;
				// Reset the time for hold soft press purposes.
				jc->press_times[softIndex] = jc->time_now;
				handleButtonChange(softIndex, false, true, softName, jc);
			}
		}
		// Else, time passes as soft press is being held, waiting to see if the soft binding should be skipped
		break;
	case DstState::QuickSoftTap:
		// Soft trigger is already released. Send release now!
		jc->triggerState[idxState] = DstState::NoPress;
		handleButtonChange(softIndex, true, false, softName, jc);
		break;
	case DstState::QuickFullPress:
		if (lastPressed == 1.0f && pressed < 1.0f) {
			// Full press is being release
			jc->triggerState[idxState] = DstState::QuickFullRelease;
			handleButtonChange(fullIndex, true, false, fullName.c_str(), jc);
		}
		else {
			// Full press is being held
			handleButtonChange(fullIndex, true, true, fullName.c_str(), jc);
		}
		break;
	case DstState::QuickFullRelease:
		if (pressed <= trigger_threshold) {
			jc->triggerState[idxState] = DstState::NoPress;
		}
		else if (pressed == 1.0f)
		{
			// Trigger is being full pressed again
			jc->triggerState[idxState] = DstState::QuickFullPress;
			handleButtonChange(fullIndex, false, true, fullName.c_str(), jc);
		}
		// else wait for the the trigger to be fully released
		break;
	case DstState::SoftPress:
		if (lastPressed > trigger_threshold && pressed <= trigger_threshold) {
			// Soft press is being released
			jc->triggerState[idxState] = DstState::NoPress;
			handleButtonChange(softIndex, true, false, softName, jc);
		}
		else // Soft Press is being held
		{
			handleButtonChange(softIndex, true, true, fullName.c_str(), jc);

			if ((mode == TriggerMode::maySkip || mode == TriggerMode::noSkip) &&
				lastPressed > trigger_threshold && lastPressed < 1.0 && pressed == 1.0)
			{
				// Full press is allowed in addition to soft press
				jc->triggerState[idxState] = DstState::DelayFullPress;
				handleButtonChange(fullIndex, false, true, fullName.c_str(), jc);
			}
			// else ignore full press on NO_FULL and MUST_SKIP
		}
		break;
	case DstState::DelayFullPress:
		if (lastPressed == 1.0 && pressed < 1.0)
		{
			// Full Press is being released
			jc->triggerState[idxState] = DstState::SoftPress;
			handleButtonChange(fullIndex, true, false, fullName.c_str(), jc);
		}
		else // Full press is being held
		{
			handleButtonChange(fullIndex, true, true, fullName.c_str(), jc);
		}
		// Soft press is always held regardless
		handleButtonChange(softIndex, true, true, fullName.c_str(), jc);
		break;
	default:
		// TODO: use magic enum to translate enum # to str
		printf("Error: Trigger %s has invalid state #%d. Reset to NoPress.\n", softName, jc->triggerState[idxState]);
		jc->triggerState[idxState] = DstState::NoPress;
		break;
	}
}

static float handleFlickStick(float calX, float calY, float lastCalX, float lastCalY, JoyShock * jc, float mouseCalibrationFactor) {
	float camSpeedX = 0.0f;
	// let's centre this
	float offsetX = calX;
	float offsetY = calY;
	float lastOffsetX = lastCalX;
	float lastOffsetY = lastCalY;
	float stickLength = sqrt(offsetX * offsetX + offsetY * offsetY);
	//printf("Flick! %.4f ", stickLength);
	float lastOffsetLength = sqrt(lastOffsetX * lastOffsetX + lastOffsetY * lastOffsetY);
	float flickStickThreshold = 1.0f - stick_deadzone_outer;
	if (stickLength >= flickStickThreshold) {
		float stickAngle = atan2(-offsetX, offsetY);
		//printf(", %.4f\n", lastOffsetLength);
		if (lastOffsetLength < flickStickThreshold) {
			// bam! new flick!
			jc->started_flick = std::chrono::steady_clock::now();
			jc->delta_flick = stickAngle;
			jc->flick_percent_done = 0.0f;
			jc->ResetSmoothSample();
			jc->flick_rotation_counter = stickAngle; // track all rotation for this flick
			// TODO: All these printfs should be hidden behind a setting. User might not want them.
			printf("Flick: %.3f degrees\n", stickAngle * (180.0f / PI));
		}
		else {
			// not new? turn camera?
			float lastStickAngle = atan2(-lastOffsetX, lastOffsetY);
			float angleChange = stickAngle - lastStickAngle;
			// https://stackoverflow.com/a/11498248/1130520
			angleChange = fmod(angleChange + PI, 2.0f * PI);
			if (angleChange < 0)
				angleChange += 2.0f * PI;
			angleChange -= PI;
			jc->flick_rotation_counter += angleChange; // track all rotation for this flick
			float flickSpeedConstant = real_world_calibration * mouseCalibrationFactor / in_game_sens;
			float flickSpeed = -(angleChange * flickSpeedConstant);
			int maxSmoothingSamples = min(jc->NumSamples, (int)(64.0f * (jc->poll_rate / 1000.0f))); // target a max smoothing window size of 64ms
			float stepSize = jc->stick_step_size; // and we only want full on smoothing when the stick change each time we poll it is approximately the minimum stick resolution
												  // the fact that we're using radians makes this really easy
			camSpeedX = jc->GetSmoothedStickRotation(flickSpeed, flickSpeedConstant * stepSize * 8.0f, flickSpeedConstant * stepSize * 16.0f, maxSmoothingSamples);
		}
	}
	else if (lastOffsetLength >= flickStickThreshold) {
		// was a flick! how much was the flick and rotation?
		last_flick_and_rotation = abs(jc->flick_rotation_counter) / (2.0 * PI);
	}
	// do the flicking
	float secondsSinceFlick = ((float)std::chrono::duration_cast<std::chrono::microseconds>(jc->time_now - jc->started_flick).count()) / 1000000.0f;
	float newPercent = secondsSinceFlick / flick_time;
	if (newPercent > 1.0f) newPercent = 1.0f;
	// warping towards 1.0
	float oldShapedPercent = 1.0f - jc->flick_percent_done;
	oldShapedPercent *= oldShapedPercent;
	oldShapedPercent = 1.0f - oldShapedPercent;
	//float oldShapedPercent = jc->flick_percent_done;
	jc->flick_percent_done = newPercent;
	newPercent = 1.0f - newPercent;
	newPercent *= newPercent;
	newPercent = 1.0f - newPercent;
	float camSpeedChange = (newPercent - oldShapedPercent) * jc->delta_flick * real_world_calibration * -mouseCalibrationFactor / in_game_sens;
	camSpeedX += camSpeedChange;

	return camSpeedX;
}

void joyShockPollCallback(int jcHandle, JOY_SHOCK_STATE state, JOY_SHOCK_STATE lastState, IMU_STATE imuState, IMU_STATE lastImuState, float deltaTime) {
	bool blockGyro = false;
	bool leftAny = false;
	bool rightAny = false;
	// get jc from handle
	JoyShock* jc = getJoyShockFromHandle(jcHandle);
	//printf("Controller %d\n", jcHandle);
	if (jc == nullptr) return;
	//printf("Found a match for %d\n", jcHandle);
	float gyroX = 0.0;
	float gyroY = 0.0;
	int mouse_x_flag = (int)mouse_x_from_gyro;
	if ((mouse_x_flag & (int)GyroAxisMask::x) > 0) {
		gyroX -= imuState.gyroX; // x axis is negative because that's what worked before, don't want to mess with definitions of "inverted"
	}
	if ((mouse_x_flag & (int)GyroAxisMask::y) > 0) {
		gyroX -= imuState.gyroY; // x axis is negative because that's what worked before, don't want to mess with definitions of "inverted"
	}
	if ((mouse_x_flag & (int)GyroAxisMask::z) > 0) {
		gyroX -= imuState.gyroZ; // x axis is negative because that's what worked before, don't want to mess with definitions of "inverted"
	}
	int mouse_y_flag = (int)mouse_y_from_gyro;
	if ((mouse_y_flag & (int)GyroAxisMask::x) > 0) {
		gyroY += imuState.gyroX;
	}
	if ((mouse_y_flag & (int)GyroAxisMask::y) > 0) {
		gyroY += imuState.gyroY;
	}
	if ((mouse_y_flag & (int)GyroAxisMask::z) > 0) {
		gyroY += imuState.gyroZ;
	}
	float gyroLength = sqrt(gyroX * gyroX + gyroY * gyroY);
	// do gyro smoothing
	// convert gyro smooth time to number of samples
	int numGyroSamples = jc->poll_rate * gyro_smooth_time; // samples per second * seconds = samples
	if (numGyroSamples < 1) numGyroSamples = 1; // need at least 1 sample
	jc->GetSmoothedGyro(gyroX, gyroY, gyroLength, gyro_smooth_threshold / 2.0f, gyro_smooth_threshold, numGyroSamples, gyroX, gyroY);
	//printf("%d Samples for threshold: %0.4f\n", numGyroSamples, gyro_smooth_threshold * maxSmoothingSamples);

	// now, honour gyro_cutoff_speed
	gyroLength = sqrt(gyroX * gyroX + gyroY * gyroY);
	if (gyro_cutoff_recovery > gyro_cutoff_speed) {
		// we can use gyro_cutoff_speed
		float gyroIgnoreFactor = (gyroLength - gyro_cutoff_speed) / (gyro_cutoff_recovery - gyro_cutoff_speed);
		if (gyroIgnoreFactor < 1.0f) {
			if (gyroIgnoreFactor <= 0.0f) {
				gyroX = gyroY = gyroLength = 0.0f;
			}
			else {
				gyroX *= gyroIgnoreFactor;
				gyroY *= gyroIgnoreFactor;
				gyroLength *= gyroIgnoreFactor;
			}
		}
	}
	else if (gyro_cutoff_speed > 0.0f && gyroLength < gyro_cutoff_speed) {
		// gyro_cutoff_recovery is something weird, so we just do a hard threshold
		gyroX = gyroY = gyroLength = 0.0f;
	}

	jc->time_now = std::chrono::steady_clock::now();
	ReleaseQueueItem item;
	while (jc->tap_release_queue.size() > 0 && (jc->tap_release_queue.front().time_queued - jc->time_now).count() < 0) {
		item = jc->tap_release_queue.front();
		pressKey(mappings[item.index], false);
		jc->tap_release_queue.pop();
	}

	// sticks!
	float camSpeedX = 0.0f;
	float camSpeedY = 0.0f;
	// account for os mouse speed and convert from radians to degrees because gyro reports in degrees per second
	float mouseCalibrationFactor = 180.0f / PI / os_mouse_speed;
	// let's do these sticks... don't want to constantly send input, so we need to compare them to last time
	float lastCalX = lastState.stickLX;
	float lastCalY = lastState.stickLY;
	float calX = state.stickLX;
	float calY = state.stickLY;
	bool lastLeftPegged = processDeadZones(lastCalX, lastCalY);
	bool leftPegged = processDeadZones(calX, calY);
	float lastAbsX = abs(lastCalX);
	float lastAbsY = abs(lastCalY);
	float absX = abs(calX);
	float absY = abs(calY);
	bool lastLeft = lastCalX < -0.5f * lastAbsY;
	bool lastRight = lastCalX > 0.5f * lastAbsY;
	bool lastDown = lastCalY < -0.5f * lastAbsX;
	bool lastUp = lastCalY > 0.5f * lastAbsX;
	bool left = calX < -0.5f * absY;
	bool right = calX > 0.5f * absY;
	bool down = calY < -0.5f * absX;
	bool up = calY > 0.5f * absX;
	if (left_stick_mode == StickMode::flick) {
		camSpeedX += handleFlickStick(calX, calY, lastCalX, lastCalY, jc, mouseCalibrationFactor);
		leftAny = leftPegged;
	}
	else if (left_stick_mode == StickMode::aim) {
		// camera movement
		if (!leftPegged) {
			jc->left_acceleration = 1.0f; // reset
		}
		float stickLength = sqrt(calX * calX + calY * calY);
		if (stickLength != 0.0f) {
			leftAny = true;
			float warpedStickLength = pow(stickLength, stick_power);
			warpedStickLength *= stick_sens * real_world_calibration / os_mouse_speed / in_game_sens;
			camSpeedX += calX / stickLength * warpedStickLength * jc->left_acceleration * deltaTime;
			camSpeedY += calY / stickLength * warpedStickLength * jc->left_acceleration * deltaTime;
			if (leftPegged) {
				jc->left_acceleration += stick_acceleration_rate * deltaTime;
				if (jc->left_acceleration > stick_acceleration_cap) {
					jc->left_acceleration = stick_acceleration_cap;
				}
			}
		}
	}
	else {
		// left!
		handleButtonChange(MAPPING_LLEFT, lastLeft, left, "LLEFT", jc);
		// right!
		handleButtonChange(MAPPING_LRIGHT, lastRight, right, "LRIGHT", jc);
		// up!
		handleButtonChange(MAPPING_LUP, lastUp, up, "LUP", jc);
		// down!
		handleButtonChange(MAPPING_LDOWN, lastDown, down, "LDOWN", jc);

		leftAny = left | right | up | down;
	}
	lastCalX = lastState.stickRX;
	lastCalY = lastState.stickRY;
	calX = state.stickRX;
	calY = state.stickRY;
	float lastRightPegged = processDeadZones(lastCalX, lastCalY);
	float rightPegged = processDeadZones(calX, calY);
	lastAbsX = abs(lastCalX);
	lastAbsY = abs(lastCalY);
	absX = abs(calX);
	absY = abs(calY);
	lastLeft = lastCalX < -0.5f * lastAbsY;
	lastRight = lastCalX > 0.5f * lastAbsY;
	lastDown = lastCalY < -0.5f * lastAbsX;
	lastUp = lastCalY > 0.5f * lastAbsX;
	left = calX < -0.5f * absY;
	right = calX > 0.5f * absY;
	down = calY < -0.5f * absX;
	up = calY > 0.5f * absX;
	if (right_stick_mode == StickMode::flick) {
		camSpeedX += handleFlickStick(calX, calY, lastCalX, lastCalY, jc, mouseCalibrationFactor);
		rightAny = rightPegged;
	}
	else if (right_stick_mode == StickMode::aim) {
		// camera movement
		if (!rightPegged) {
			jc->right_acceleration = 1.0f; // reset
		}
		float stickLength = sqrt(calX * calX + calY * calY);
		if (stickLength > 0.0f) {
			rightAny = true;
			float warpedStickLength = pow(stickLength, stick_power);
			warpedStickLength *= stick_sens * real_world_calibration / os_mouse_speed / in_game_sens;
			camSpeedX += calX / stickLength * warpedStickLength * jc->right_acceleration * deltaTime;
			camSpeedY += calY / stickLength * warpedStickLength * jc->right_acceleration * deltaTime;
			if (rightPegged) {
				jc->right_acceleration += stick_acceleration_rate * deltaTime;
				if (jc->right_acceleration > stick_acceleration_cap) {
					jc->right_acceleration = stick_acceleration_cap;
				}
			}
		}
	}
	else {
		// left!
		handleButtonChange(MAPPING_RLEFT, lastLeft, left, "RLEFT", jc);
		// right!
		handleButtonChange(MAPPING_RRIGHT, lastRight, right, "RRIGHT", jc);
		// up!
		handleButtonChange(MAPPING_RUP, lastUp, up, "RUP", jc);
		// down!
		handleButtonChange(MAPPING_RDOWN, lastDown, down, "RDOWN", jc);

		rightAny = left | right | up | down;
	}

	switch (gyro_ignore_mode) {
	case GyroIgnoreMode::none:
		blockGyro = false;
		break;
	case GyroIgnoreMode::button:
		blockGyro = (gyro_button_enables ^ ((state.buttons & gyro_button) != 0));
		break;
	case GyroIgnoreMode::left:
		blockGyro = (gyro_button_enables ^ leftAny);
		break;
	case GyroIgnoreMode::right:
		blockGyro = (gyro_button_enables ^ rightAny);
		break;
	}

	if (blockGyro) {
		gyroX = 0;
		gyroY = 0;
	}
	// optionally ignore the gyro of one of the joycons
	if (jc->controller_type == JS_SPLIT_TYPE_FULL ||
		(jc->controller_type & (int)joycon_gyro_mask) == 0)
	{
		//printf("GX: %0.4f GY: %0.4f GZ: %0.4f\n", imuState.gyroX, imuState.gyroY, imuState.gyroZ);
		float mouseCalibration = real_world_calibration / os_mouse_speed / in_game_sens;
		shapedSensitivityMoveMouse(gyroX * gyro_x_sign, gyroY * gyro_y_sign, min_gyro_sens, max_gyro_sens, min_gyro_threshold, max_gyro_threshold, deltaTime, camSpeedX * aim_x_sign, -camSpeedY * aim_y_sign, mouseCalibration);
	}

	// button mappings
	handleButtonChange(MAPPING_UP, (lastState.buttons & JSMASK_UP) > 0, (state.buttons & JSMASK_UP) > 0, "UP", jc);
	handleButtonChange(MAPPING_DOWN, (lastState.buttons & JSMASK_DOWN) > 0, (state.buttons & JSMASK_DOWN) > 0, "DOWN", jc);
	handleButtonChange(MAPPING_LEFT, (lastState.buttons & JSMASK_LEFT) > 0, (state.buttons & JSMASK_LEFT) > 0, "LEFT", jc);
	handleButtonChange(MAPPING_RIGHT, (lastState.buttons & JSMASK_RIGHT) > 0, (state.buttons & JSMASK_RIGHT) > 0, "RIGHT", jc);
	handleButtonChange(MAPPING_L, (lastState.buttons & JSMASK_L) > 0, (state.buttons & JSMASK_L) > 0, "L", jc);
	handleTriggerChange(MAPPING_ZL, MAPPING_ZLF, zlMode, lastState.lTrigger, state.lTrigger, "ZL", jc);
	handleButtonChange(MAPPING_MINUS, (lastState.buttons & JSMASK_MINUS) > 0, (state.buttons & JSMASK_MINUS) > 0, "MINUS", jc);
	handleButtonChange(MAPPING_CAPTURE, (lastState.buttons & JSMASK_CAPTURE) > 0, (state.buttons & JSMASK_CAPTURE) > 0, "CAPTURE", jc);
	handleButtonChange(MAPPING_E, (lastState.buttons & JSMASK_E) > 0, (state.buttons & JSMASK_E) > 0, "E", jc);
	handleButtonChange(MAPPING_S, (lastState.buttons & JSMASK_S) > 0, (state.buttons & JSMASK_S) > 0, "S", jc);
	handleButtonChange(MAPPING_N, (lastState.buttons & JSMASK_N) > 0, (state.buttons & JSMASK_N) > 0, "N", jc);
	handleButtonChange(MAPPING_W, (lastState.buttons & JSMASK_W) > 0, (state.buttons & JSMASK_W) > 0, "W", jc);
	handleButtonChange(MAPPING_R, (lastState.buttons & JSMASK_R) > 0, (state.buttons & JSMASK_R) > 0, "R", jc);
	handleTriggerChange(MAPPING_ZR, MAPPING_ZRF, zrMode, lastState.rTrigger, state.rTrigger, "ZR", jc);
	handleButtonChange(MAPPING_PLUS, (lastState.buttons & JSMASK_PLUS) > 0, (state.buttons & JSMASK_PLUS) > 0, "PLUS", jc);
	handleButtonChange(MAPPING_HOME, (lastState.buttons & JSMASK_HOME) > 0, (state.buttons & JSMASK_HOME) > 0, "HOME", jc);
	handleButtonChange(MAPPING_SL, (lastState.buttons & JSMASK_SL) > 0, (state.buttons & JSMASK_SL) > 0, "SL", jc);
	handleButtonChange(MAPPING_SR, (lastState.buttons & JSMASK_SR) > 0, (state.buttons & JSMASK_SR) > 0, "SR", jc);
	handleButtonChange(MAPPING_L3, (lastState.buttons & JSMASK_LCLICK) > 0, (state.buttons & JSMASK_LCLICK) > 0, "L3", jc);
	handleButtonChange(MAPPING_R3, (lastState.buttons & JSMASK_RCLICK) > 0, (state.buttons & JSMASK_RCLICK) > 0, "R3", jc);
}

void connectDevices() {
	int numConnected = JslConnectDevices();
	int* deviceHandles = new int[numConnected];

	JslGetConnectedDeviceHandles(deviceHandles, numConnected);

	for (int i = 0; i < numConnected; i++) {
		// map handles to extra local data
		int handle = deviceHandles[i];
		JoyShock* js = new JoyShock(handle,
			JslGetPollRate(handle),
			JslGetControllerSplitType(handle),
			JslGetStickStep(handle));
		handle_to_joyshock.emplace(deviceHandles[i], js);

		// calibration?
		//JslStartContinuousCalibration(deviceHandles[i]);
	}

	if (numConnected == 1) {
		printf("1 device connected\n");
	}
	else {
		printf("%i devices connected\n", numConnected);
	}

	//if (numConnected != 0) {
	//	printf("All devices have started continuous gyro calibration\n");
	//}

	delete[] deviceHandles;
}

// https://stackoverflow.com/a/25311622/1130520 says this is why filenames obtained by fgets don't work
static void removeNewLine(char* string) {
	char *p;
	if ((p = strchr(string, '\n')) != NULL) {
		*p = '\0'; /* remove newline */
	}
}

bool AutoLoadPoll(void *param)
{
	static std::string lastModuleName;
	std::string windowTitle, windowModule;
	std::tie(windowModule, windowTitle) = GetActiveWindowName();
	if (!windowModule.empty() && windowModule != lastModuleName && windowModule.compare("JoyShockMapper.exe") != 0)
	{
		lastModuleName = windowModule;
		std::string cwd(GetCWD());
		if (!cwd.empty())
		{
			cwd.append("\\AutoLoad\\");
			auto files = ListDirectory(cwd);
			auto noextmodule = windowModule.substr(0, windowModule.find_first_of('.'));
			printf("[AUTOLOAD] \"%s\" in focus: ", windowTitle.c_str()); // looking for config : " , );
			noextmodule = toLower(noextmodule);
			bool success = false;
			for (auto file : files)
			{
				auto noextconfig = file.substr(0, file.find_first_of('.'));
				noextconfig = toLower(noextconfig);
				if (noextconfig == noextmodule)
				{
					// TODO: add mutex to avoid threading issues
					printf("autoload configuration exists.\n");
					parseCommand(cwd + file);
					success = true;
					break;
				}
			}
			if (!success)
			{
				printf("create \"AutoLoad\\%s.txt\" to autoload for this application.\n", noextmodule.c_str());
			}
		}
		else
		{
			printf("[AUTOLOAD] ERROR could not load CWD. Disabling AUTOLOAD.");
			return false;
		}
	}
	return true;
}

//int main(int argc, char *argv[]) {
int wWinMain(HINSTANCE hInstance, HINSTANCE prevInstance, LPWSTR cmdLine, int cmdShow) {

	// console
	initConsole();
	printf("Welcome to JoyShockMapper version %s!\n", version);
	// prepare for input
	resetAllMappings();
	connectDevices();
	JslSetCallback(&joyShockPollCallback);
	// Future Development!
	autoLoadThread.reset(new PollingThread(&AutoLoadPoll, nullptr, 1000, true)); // Start by default
	if (*autoLoadThread) printf("AutoLoad is enabled. Configurations in \"AutoLoad\" folder will get loaded when matching application is in focus.\n");
	else printf("[AUTOLOAD] AutoLoad is unavailable\n");
	// poll joycons:
	while (true) {
		fgets(tempConfigName, 128, stdin);
		removeNewLine(tempConfigName);
		if (strcmp(tempConfigName, "QUIT") == 0) {
			break;
		}
		else {
			parseCommand(tempConfigName);
		}
		//pollLoop();
	}
	JslDisconnectAndDisposeAll();
	return 0;
}
