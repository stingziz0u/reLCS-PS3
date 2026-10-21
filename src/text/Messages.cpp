#include "common.h"

#include "Messages.h"
#include "RwHelper.h"
#include "Hud.h"
#include "User.h"
#include "Timer.h"
#include "Text.h"

#include "ControllerConfig.h"

#include "Font.h"

#include "Pad.h"

tMessage CMessages::BriefMessages[NUMBRIEFMESSAGES];
tPreviousBrief CMessages::PreviousBriefs[NUMPREVIOUSBRIEFS];
tBigMessage CMessages::BIGMessages[NUMBIGMESSAGES];
char CMessages::PreviousMissionTitle[16]; // unused
bool CMessages::MissionTitleWaitPending;

void
CMessages::Init()
{
	ClearMessages();

	for (int32 i = 0; i < NUMPREVIOUSBRIEFS; i++) {
		PreviousBriefs[i].m_pText = nil;
		PreviousBriefs[i].m_pString = nil;
	}
}

uint16
CMessages::GetWideStringLength(wchar *src)
{
	uint16 length = 0;
	while (*(src++)) length++;
	return length;
}

void
CMessages::WideStringCopy(wchar *dst, wchar *src, uint16 size)
{
	int32 i = 0;
	if (src) {
		while (i < size - 1) {
			if (!src[i]) break;
			dst[i] = src[i];
			i++;
		}
	} else {
		while (i < size - 1)
			dst[i++] = '\0';
	}
	dst[i] = '\0';
}

wchar FixupChar(wchar c)
{
#ifdef MORE_LANGUAGES
	if (CFont::IsJapanese())
		return c & 0x7fff;
#endif
	return c;
}

bool
CMessages::WideStringCompare(wchar *str1, wchar *str2, uint16 size)
{
	uint16 len1 = GetWideStringLength(str1);
	uint16 len2 = GetWideStringLength(str2);
	if (len1 != len2 && (len1 < size || len2 < size))
		return false;

	for (int32 i = 0; i < size && FixupChar(str1[i]) != '\0'; i++) {
		if (FixupChar(str1[i]) != FixupChar(str2[i]))
			return false;
	}
	return true;
}

bool
CMessages::IsRaceBigMessage(wchar *text)
{
	if (text == nil)
		return false;

	static const char *raceKeys[] = {
		"RACE_FL", "RACE_Y", "RACE_Y1", "RACE_Y2", "RACE_Y3",
		"TRCR1", "TRCR2", "TRCR3", "TRCRGO",
		"MRACEC1", "MRACEC2", "MRACEC3", "MRACEGO",
	};
	for (uint32 i = 0; i < ARRAY_SIZE(raceKeys); i++)
		if (text == TheText.Get(raceKeys[i]))
			return true;
	return false;
}

bool
CMessages::ConsumeMissionTitleScriptWait(void)
{
	if (!MissionTitleWaitPending)
		return false;
	MissionTitleWaitPending = false;
	return true;
}

static uint32
GetBigMessageTime(wchar *text, uint32 time, uint16 style)
{
	if (text == TheText.Get("RACE_FL"))
		return 1000u;
	return style == 1 ? Max(time, 1500u) : time;
}

void
CMessages::Process()
{
	for (int32 style = 0; style < 6; style++) {
		if (BIGMessages[style].m_Stack[0].m_pText != nil && CTimer::GetTimeInMilliseconds() > BIGMessages[style].m_Stack[0].m_nTime + BIGMessages[style].m_Stack[0].m_nStartTime) {
			BIGMessages[style].m_Stack[0].m_pText = nil;

			int32 i = 0;
			while (i < 3) {
				if (BIGMessages[style].m_Stack[i + 1].m_pText == nil) break;
				BIGMessages[style].m_Stack[i] = BIGMessages[style].m_Stack[i + 1];
				i++;
			}

			BIGMessages[style].m_Stack[i].m_pText = nil;
			BIGMessages[style].m_Stack[0].m_nStartTime = CTimer::GetTimeInMilliseconds();
		}
	}

	if (BriefMessages[0].m_pText != nil && CTimer::GetTimeInMilliseconds() > BriefMessages[0].m_nTime + BriefMessages[0].m_nStartTime) {
		BriefMessages[0].m_pText = nil;
		int32 i;
		for (i = 0; i < NUMBRIEFMESSAGES-1 && BriefMessages[i + 1].m_pText != nil; i++) {
			BriefMessages[i] = BriefMessages[i + 1];
		}
		CMessages::BriefMessages[i].m_pText = nil;
		CMessages::BriefMessages[0].m_nStartTime = CTimer::GetTimeInMilliseconds();
		if (BriefMessages[0].m_pText != nil)
			AddToPreviousBriefArray(
				BriefMessages[0].m_pText,
				BriefMessages[0].m_nNumber[0],
				BriefMessages[0].m_nNumber[1],
				BriefMessages[0].m_nNumber[2],
				BriefMessages[0].m_nNumber[3],
				BriefMessages[0].m_nNumber[4],
				BriefMessages[0].m_nNumber[5],
				BriefMessages[0].m_pString);
	}
}

void
CMessages::Display()
{
	wchar outstr[256];

	DefinedState();

	for (int32 i = 0; i < NUMBIGMESSAGES; i++) {
		InsertNumberInString(
			BIGMessages[i].m_Stack[0].m_pText,
			BIGMessages[i].m_Stack[0].m_nNumber[0],
			BIGMessages[i].m_Stack[0].m_nNumber[1],
			BIGMessages[i].m_Stack[0].m_nNumber[2],
			BIGMessages[i].m_Stack[0].m_nNumber[3],
			BIGMessages[i].m_Stack[0].m_nNumber[4],
			BIGMessages[i].m_Stack[0].m_nNumber[5],
			outstr);
		InsertStringInString(outstr, BIGMessages[i].m_Stack[0].m_pString);
		InsertPlayerControlKeysInString(outstr);
		if (BIGMessages[i].m_Stack[0].m_pText != nil) {
			const bool useExactTiming = IsRaceBigMessage(BIGMessages[i].m_Stack[0].m_pText);
			if (i < 6 && CHud::BigMessageUsesExactTiming[i] && !useExactTiming) {
				BigMessageInUse[i] = 0.0f;
				CHud::BigMessageAlpha[i] = 0.0f;
			}
			CHud::BigMessageUsesExactTiming[i] = useExactTiming;
		}
		CHud::BigMessageDuration[i] = CHud::BigMessageUsesExactTiming[i]
			? BIGMessages[i].m_Stack[0].m_nTime
			: Max(BIGMessages[i].m_Stack[0].m_nTime, 1500u);
		CHud::SetBigMessage(outstr, i);
	}

	InsertNumberInString(
		BriefMessages[0].m_pText,
		BriefMessages[0].m_nNumber[0],
		BriefMessages[0].m_nNumber[1],
		BriefMessages[0].m_nNumber[2],
		BriefMessages[0].m_nNumber[3],
		BriefMessages[0].m_nNumber[4],
		BriefMessages[0].m_nNumber[5],
		outstr);
	InsertStringInString(outstr, BriefMessages[0].m_pString);
	InsertPlayerControlKeysInString(outstr);
	CHud::SetMessage(outstr);
}

void
CMessages::AddMessage(wchar *msg, uint32 time, uint16 flag)
{
	wchar outstr[512]; // unused
	WideStringCopy(outstr, msg, 256);
	InsertPlayerControlKeysInString(outstr);
	GetWideStringLength(outstr);

	int32 i = 0;
	while (i < NUMBRIEFMESSAGES && BriefMessages[i].m_pText != nil)
		i++;
	if (i >= NUMBRIEFMESSAGES) return;

	BriefMessages[i].m_pText = msg;
	BriefMessages[i].m_nFlag = flag;
	BriefMessages[i].m_nTime = time;
	BriefMessages[i].m_nStartTime = CTimer::GetTimeInMilliseconds();
	BriefMessages[i].m_nNumber[0] = -1;
	BriefMessages[i].m_nNumber[1] = -1;
	BriefMessages[i].m_nNumber[2] = -1;
	BriefMessages[i].m_nNumber[3] = -1;
	BriefMessages[i].m_nNumber[4] = -1;
	BriefMessages[i].m_nNumber[5] = -1;
	BriefMessages[i].m_pString = nil;
	if (i == 0)
		AddToPreviousBriefArray(
			BriefMessages[0].m_pText,
			BriefMessages[0].m_nNumber[0],
			BriefMessages[0].m_nNumber[1],
			BriefMessages[0].m_nNumber[2],
			BriefMessages[0].m_nNumber[3],
			BriefMessages[0].m_nNumber[4],
			BriefMessages[0].m_nNumber[5],
			BriefMessages[0].m_pString);
}

void
CMessages::AddMessageJumpQ(wchar *msg, uint32 time, uint16 flag)
{
	wchar outstr[512]; // unused
	WideStringCopy(outstr, msg, 256);
	InsertPlayerControlKeysInString(outstr);
	GetWideStringLength(outstr);

	BriefMessages[0].m_pText = msg;
	BriefMessages[0].m_nFlag = flag;
	BriefMessages[0].m_nTime = time;
	BriefMessages[0].m_nStartTime = CTimer::GetTimeInMilliseconds();
	BriefMessages[0].m_nNumber[0] = -1;
	BriefMessages[0].m_nNumber[1] = -1;
	BriefMessages[0].m_nNumber[2] = -1;
	BriefMessages[0].m_nNumber[3] = -1;
	BriefMessages[0].m_nNumber[4] = -1;
	BriefMessages[0].m_nNumber[5] = -1;
	BriefMessages[0].m_pString = nil;
	AddToPreviousBriefArray(msg, -1, -1, -1, -1, -1, -1, 0);
}

void
CMessages::AddMessageSoon(wchar *msg, uint32 time, uint16 flag)
{
	wchar outstr[512]; // unused
	WideStringCopy(outstr, msg, 256);
	InsertPlayerControlKeysInString(outstr);
	GetWideStringLength(outstr);

	if (BriefMessages[0].m_pText != nil) {
		for (int i = NUMBRIEFMESSAGES-1; i > 1; i--)
			BriefMessages[i] = BriefMessages[i-1];

		BriefMessages[1].m_pText = msg;
		BriefMessages[1].m_nFlag = flag;
		BriefMessages[1].m_nTime = time;
		BriefMessages[1].m_nStartTime = CTimer::GetTimeInMilliseconds();
		BriefMessages[1].m_nNumber[0] = -1;
		BriefMessages[1].m_nNumber[1] = -1;
		BriefMessages[1].m_nNumber[2] = -1;
		BriefMessages[1].m_nNumber[3] = -1;
		BriefMessages[1].m_nNumber[4] = -1;
		BriefMessages[1].m_nNumber[5] = -1;
		BriefMessages[1].m_pString = nil;
	}else{
		BriefMessages[0].m_pText = msg;
		BriefMessages[0].m_nFlag = flag;
		BriefMessages[0].m_nTime = time;
		BriefMessages[0].m_nStartTime = CTimer::GetTimeInMilliseconds();
		BriefMessages[0].m_nNumber[0] = -1;
		BriefMessages[0].m_nNumber[1] = -1;
		BriefMessages[0].m_nNumber[2] = -1;
		BriefMessages[0].m_nNumber[3] = -1;
		BriefMessages[0].m_nNumber[4] = -1;
		BriefMessages[0].m_nNumber[5] = -1;
		BriefMessages[0].m_pString = nil;
		AddToPreviousBriefArray(msg, -1, -1, -1, -1, -1, -1, nil);
	}
}

void
CMessages::ClearMessages()
{
	MissionTitleWaitPending = false;
	for (int32 i = 0; i < NUMBIGMESSAGES; i++) {
		for (int32 j = 0; j < 4; j++) {
			BIGMessages[i].m_Stack[j].m_pText = nil;
			BIGMessages[i].m_Stack[j].m_pString = nil;
		}
	}
	ClearSmallMessagesOnly();
}

void
CMessages::ClearSmallMessagesOnly()
{
	for (int32 i = 0; i < NUMBRIEFMESSAGES; i++) {
		BriefMessages[i].m_pText = nil;
		BriefMessages[i].m_pString = nil;
	}
}

void
CMessages::AddBigMessage(wchar *msg, uint32 time, uint16 style)
{
	time = GetBigMessageTime(msg, time, style);
	if (style == 1)
		MissionTitleWaitPending = true;
	wchar outstr[512]; // unused
	WideStringCopy(outstr, msg, 256);
	InsertPlayerControlKeysInString(outstr);
	GetWideStringLength(outstr);

	BIGMessages[style].m_Stack[0].m_pText = msg;
	BIGMessages[style].m_Stack[0].m_nFlag = 0;
	BIGMessages[style].m_Stack[0].m_nTime = time;
	BIGMessages[style].m_Stack[0].m_nStartTime = CTimer::GetTimeInMilliseconds();
	BIGMessages[style].m_Stack[0].m_nNumber[0] = -1;
	BIGMessages[style].m_Stack[0].m_nNumber[1] = -1;
	BIGMessages[style].m_Stack[0].m_nNumber[2] = -1;
	BIGMessages[style].m_Stack[0].m_nNumber[3] = -1;
	BIGMessages[style].m_Stack[0].m_nNumber[4] = -1;
	BIGMessages[style].m_Stack[0].m_nNumber[5] = -1;
	BIGMessages[style].m_Stack[0].m_pString = nil;
}

void
CMessages::AddBigMessageQ(wchar *msg, uint32 time, uint16 style)
{
	time = GetBigMessageTime(msg, time, style);
	if (style == 1)
		MissionTitleWaitPending = true;
	wchar outstr[512]; // unused
	WideStringCopy(outstr, msg, 256);
	InsertPlayerControlKeysInString(outstr);
	GetWideStringLength(outstr);

	int32 i = 0;
	while (i < 4 && BIGMessages[style].m_Stack[i].m_pText != nil)
		i++;

	if (i >= 4) return;

	BIGMessages[style].m_Stack[i].m_pText = msg;
	BIGMessages[style].m_Stack[i].m_nFlag = 0;
	BIGMessages[style].m_Stack[i].m_nTime = time;
	BIGMessages[style].m_Stack[i].m_nStartTime = CTimer::GetTimeInMilliseconds();
	BIGMessages[style].m_Stack[i].m_nNumber[0] = -1;
	BIGMessages[style].m_Stack[i].m_nNumber[1] = -1;
	BIGMessages[style].m_Stack[i].m_nNumber[2] = -1;
	BIGMessages[style].m_Stack[i].m_nNumber[3] = -1;
	BIGMessages[style].m_Stack[i].m_nNumber[4] = -1;
	BIGMessages[style].m_Stack[i].m_nNumber[5] = -1;
	BIGMessages[style].m_Stack[i].m_pString = nil;
}

void
CMessages::AddToPreviousBriefArray(wchar *text, int32 n1, int32 n2, int32 n3, int32 n4, int32 n5, int32 n6, wchar *string)
{
	int32 i;
	for (i = 0; i < NUMPREVIOUSBRIEFS && PreviousBriefs[i].m_pText != nil; i++) {
		if (PreviousBriefs[i].m_nNumber[0] == n1
			&& PreviousBriefs[i].m_nNumber[1] == n2
			&& PreviousBriefs[i].m_nNumber[2] == n3
			&& PreviousBriefs[i].m_nNumber[3] == n4
			&& PreviousBriefs[i].m_nNumber[4] == n5
			&& PreviousBriefs[i].m_nNumber[5] == n6
			&& PreviousBriefs[i].m_pText == text
			&& PreviousBriefs[i].m_pString == string)
			return;
	}

	if (i != 0) {
		if (i == NUMPREVIOUSBRIEFS) i -= 2;
		else i--;

		while (i >= 0) {
			PreviousBriefs[i + 1] = PreviousBriefs[i];
			i--;
		}
	}
	PreviousBriefs[0].m_pText = text;
	PreviousBriefs[0].m_nNumber[0] = n1;
	PreviousBriefs[0].m_nNumber[1] = n2;
	PreviousBriefs[0].m_nNumber[2] = n3;
	PreviousBriefs[0].m_nNumber[3] = n4;
	PreviousBriefs[0].m_nNumber[4] = n5;
	PreviousBriefs[0].m_nNumber[5] = n6;
	PreviousBriefs[0].m_pString = string;
}

void
CMessages::InsertNumberInString(wchar *str, int32 n1, int32 n2, int32 n3, int32 n4, int32 n5, int32 n6, wchar *outstr)
{
	char numStr[10];
	wchar wNumStr[10];

	if (str == nil) {
		*outstr = '\0';
		return;
	}

	sprintf(numStr, "%d", n1);
	size_t outLen = strlen(numStr);
	AsciiToUnicode(numStr, wNumStr);
	if (str[0] == 0) {
		*outstr = '\0';
		return;
	}

	int32 size = GetWideStringLength(str);

	int32 i = 0;

	for (int32 c = 0; c < size;) {
#ifdef MORE_LANGUAGES
		if ((CFont::IsJapanese() && str[c] == (0x8000 | '~') && str[c + 1] == (0x8000 | '1') && str[c + 2] == (0x8000 | '~')) ||
			(!CFont::IsJapanese() && str[c] == '~' && str[c + 1] == '1' && str[c + 2] == '~')) {
#else
		if (str[c] == '~' && str[c + 1] == '1' && str[c + 2] == '~') {
#endif
			c += 3;
			for (int j = 0; j < outLen; )
				*(outstr++) = wNumStr[j++];

			i++;
			switch (i) {
			case 1: sprintf(numStr, "%d", n2); break;
			case 2: sprintf(numStr, "%d", n3); break;
			case 3: sprintf(numStr, "%d", n4); break;
			case 4: sprintf(numStr, "%d", n5); break;
			case 5: sprintf(numStr, "%d", n6); break;
			}
			outLen = strlen(numStr);
			AsciiToUnicode(numStr, wNumStr);
		} else {
			*(outstr++) = str[c++];
		}
	}
	*outstr = '\0';
}

void
CMessages::InsertStringInString(wchar *str1, wchar *str2)
{
	wchar tempstr[256];

	if (!str1 || !str2) return;

	int32 str1_size = GetWideStringLength(str1);
	int32 str2_size = GetWideStringLength(str2);
	int32 total_size = str1_size + str2_size;
	
	wchar *_str1 = str1;
	uint16 i;
	for (i = 0; i < total_size; ) {
#ifdef MORE_LANGUAGES
		if ((CFont::IsJapanese() && *_str1 == (0x8000 | '~') && *(_str1 + 1) == (0x8000 | 'a') && *(_str1 + 2) == (0x8000 | '~'))
			|| (*_str1 == '~' && *(_str1 + 1) == 'a' && *(_str1 + 2) == '~')) {
#else
		if (*_str1 == '~' && *(_str1 + 1) == 'a' && *(_str1 + 2) == '~') {
#endif
			_str1 += 3;
			for (int j = 0; j < str2_size; j++) {
				tempstr[i++] = str2[j];
			}
		} else {
			tempstr[i++] = *(_str1++);
		}
	}
	tempstr[i] = '\0';

	for (i = 0; i < total_size; i++)
		str1[i] = tempstr[i];

	while (i < 256)
		str1[i++] = '\0';
}

int
CMessages::GetTokenPadKeyString(const wchar *in, wchar *out)
{
	wchar str[256];
	memset(str, 0, sizeof(str));
	str[0] = 'C';

	// TODO: there was a switch here but that's stupid
	str[1] = CPad::GetPad(0)->Mode + 48;

	while (*in != '~') in++;
	in++;

	int i = 1;
	while (*in != '~')
		str[1+i++] = *(in++);

	wchar *text = TheText.Get(UnicodeToAscii(str));
	if (!text) return i;
	while (text[0] != '\0')
	{
		if (text[0] == '~')
		{
			switch (text[1])
			{
			case 'L':
				*(out++) = 'M';
				break;
			case 'N':
				*(out++) = 'O';
				break;
			case 'O':
				*(out++) = 227;
				break;
			case 'R':
				*(out++) = 'S';
				break;
			case 'S':
				*(out++) = 225;
				break;
			case 'T':
				*(out++) = 224;
				break;
			case 'X':
				*(out++) = 226;
				break;
			default:
				break;
			}
			text += 3;
		}
		else {
			*(out++) = *(text++);
		}
	}
	return i;
}

#ifdef _3DS
static bool
Is3DSTouchInstruction(const wchar *text)
{
	static const char prefix[] = "TOUCH, THEN TAP ";
	for (uint32 i = 0; i < sizeof(prefix) - 1; i++)
		if (text[i] != prefix[i])
			return false;
	return true;
}

static wchar*
Normalize3DSTouchInstructionPrefix(wchar *begin, wchar *end)
{
	struct tRewrite { const char *from, *to; };
	static const tRewrite rewrites[] = {
		{ "by pressing~h~ ", "when you~h~ " },
		{ "Press the ~h~", "~h~" }, { "Press the ~w~", "~w~" },
		{ "Press the~h~ ", "~h~" }, { "Press~h~ ", "~h~" },
		{ "press the ~h~", "~h~" }, { "press the~h~ ", "~h~" },
		{ "press~h~ ", "~h~" }, { "tap the ~h~", "~h~" },
		{ "tap~h~ ", "~h~" }, { "Use~h~ ", "~h~" }, { "Use the ", "" },
	};
	for (uint32 i = 0; i < ARRAY_SIZE(rewrites); i++) {
		const uint16 fromSize = strlen(rewrites[i].from);
		if (end - begin < fromSize)
			continue;
		bool match = true;
		for (uint16 j = 0; j < fromSize; j++)
			if (end[-fromSize + j] != rewrites[i].from[j]) {
				match = false;
				break;
			}
		if (!match)
			continue;
		end -= fromSize;
		for (const char *p = rewrites[i].to; *p; p++)
			*(end++) = *p;
		break;
	}
	return end;
}

static uint16
Get3DSControlTokenString(const wchar *in, wchar *out)
{
	char token[ACTIONNAME_LENGTH + 1];
	const uint16 tokenOffset = in[0] == '~' ? 1 : 0;
	uint16 tokenSize = 0;
	while (in[tokenOffset + tokenSize] != '\0' && in[tokenOffset + tokenSize] != '~' && tokenSize < ACTIONNAME_LENGTH) {
		token[tokenSize] = (char)in[tokenOffset + tokenSize];
		tokenSize++;
	}
	token[tokenSize] = '\0';

	struct tTokenAction {
		const char *token;
		int16 firstAction;
		int16 secondAction;
	};
	static const tTokenAction actionTokens[] = {
		{ "ANS", PED_ANSWER_PHONE, -1 },
		{ "CVEIW", CAMERA_CHANGE_VIEW_ALL_SITUATIONS, -1 },
		{ "FREE1", PED_DUCK, -1 },
		{ "PDCTL", PED_CYCLE_TARGET_LEFT, PED_CYCLE_TARGET_RIGHT },
		{ "PDCWE", PED_CYCLE_WEAPON_LEFT, PED_CYCLE_WEAPON_RIGHT },
		{ "PDFW", PED_FIREWEAPON, -1 },
		{ "PDLT", PED_LOCK_TARGET, -1 },
		{ "PDSPR", PED_SPRINT, -1 },
		{ "PED_FIREWEAPON", PED_FIREWEAPON, -1 },
		/* LCS uses the same mode-dependent control as answering the phone
		 * for accepting a weapon pickup. */
		{ "PUCF", PED_ANSWER_PHONE, -1 },
		{ "SNZI", PED_SNIPER_ZOOM_IN, -1 },
		{ "SNZO", PED_SNIPER_ZOOM_OUT, -1 },
		{ "TGSUB", TOGGLE_SUBMISSIONS, -1 },
		{ "VEACC", VEHICLE_ACCELERATE, -1 },
		{ "VEBRK", VEHICLE_BRAKE, -1 },
		{ "VECRS", VEHICLE_CHANGE_RADIO_STATION, -1 },
		{ "VEEE", VEHICLE_ENTER_EXIT, -1 },
		{ "VEHB", VEHICLE_HANDBRAKE, -1 },
		{ "VEHN", VEHICLE_HORN, -1 },
		{ "VELB", VEHICLE_LOOKLEFT, VEHICLE_LOOKRIGHT },
		{ "VELL", VEHICLE_LOOKLEFT, -1 },
		{ "VELR", VEHICLE_LOOKRIGHT, -1 },
		{ "VEWEP", VEHICLE_FIREWEAPON, -1 },
	};
	struct tFixedToken {
		const char *token;
		const char *label;
	};
	static const tFixedToken fixedTokens[] = {
		{ "AMBUY", "A" },
		{ "AMEXI", "B" },
		{ "AMMOV", "CIRCLE PAD" },
		{ "FREE2", "CIRCLE PAD" },
		{ "PDLOO", "C-STICK" },
		{ "TRSK", "A" },
		{ "VESTR", "CIRCLE PAD" },
		{ "VEWEA", "C-STICK" },
		{ "VEWEI", "CIRCLE PAD UP / DOWN" },
	};

	for (uint32 i = 0; i < ARRAY_SIZE(fixedTokens); i++) {
		if (strcmp(token, fixedTokens[i].token) == 0) {
			AsciiToUnicode(fixedTokens[i].label, out);
			return tokenOffset + tokenSize;
		}
	}
	for (uint32 i = 0; i < ARRAY_SIZE(actionTokens); i++) {
		if (strcmp(token, actionTokens[i].token) != 0)
			continue;
		ControlsManager.GetWideStringOfCommandKeys(actionTokens[i].firstAction, out, 256);
		if (actionTokens[i].secondAction >= 0) {
			wchar second[32];
			memset(second, 0, sizeof(second));
			ControlsManager.GetWideStringOfCommandKeys(actionTokens[i].secondAction, second, ARRAY_SIZE(second));
			uint16 outSize = CMessages::GetWideStringLength(out);
			if (outSize != 0 && second[0] != '\0') {
				out[outSize++] = ' ';
				out[outSize++] = '+';
				out[outSize++] = ' ';
				CMessages::WideStringCopy(&out[outSize], second, 256 - outSize);
			}
		}
		return tokenOffset + tokenSize;
	}

	/* Also accept reVC-style full action names used by a few carried-over
	 * strings and by any future corrected text archive. */
	for (int32 action = 0; action < MAX_CONTROLLERACTIONS; action++) {
		uint16 actionSize = CMessages::GetWideStringLength(ControlsManager.m_aActionNames[action]);
		if (actionSize == tokenSize &&
			CMessages::WideStringCompare((wchar*)&in[tokenOffset], ControlsManager.m_aActionNames[action], actionSize)) {
			ControlsManager.GetWideStringOfCommandKeys(action, out, 256);
			return tokenOffset + tokenSize;
		}
	}

	return tokenOffset + tokenSize;
}
#endif

void
CMessages::InsertPlayerControlKeysInString(wchar *str)
{
	uint16 i;
	wchar outstr[256];
	wchar keybuf[256];

	if (!str) return;
	uint16 strSize = GetWideStringLength(str);
	memset(keybuf, 0, 256*sizeof(wchar)); // not memset? :O

	wchar *_outstr = outstr;
	for (i = 0; i < strSize;) {
#ifdef MORE_LANGUAGES
		if (i + 2 < strSize && ((CFont::IsJapanese() && str[i] == (0x8000 | '~') && str[i + 1] == (0x8000 | 'k') && str[i + 2] == (0x8000 | '~')) ||
			(!CFont::IsJapanese() && str[i] == '~' && str[i + 1] == 'k' && str[i + 2] == '~')) {
#else
		if (i + 2 < strSize && str[i] == '~' && str[i + 1] == 'k' && str[i + 2] == '~') {
#endif
			memset(keybuf, 0, 256 * sizeof(wchar));
			i += 4;
#ifdef _3DS
			const uint16 tokenStart = i;
			uint16 tokenSpan = Get3DSControlTokenString(&str[i], keybuf);
			i += tokenSpan + 1;
			uint16 keybufSize = GetWideStringLength(keybuf);
			if (keybufSize != 0 && Is3DSTouchInstruction(keybuf))
				_outstr = Normalize3DSTouchInstructionPrefix(outstr, _outstr);
			if (keybufSize == 0) {
				/* Keep an unknown token visible for diagnosis without falling back
				 * to the PS2-specific C0/C1 text entries. */
				const uint16 tokenOffset = str[tokenStart] == '~' ? 1 : 0;
				*(_outstr++) = '[';
				for (uint16 j = 0; j < tokenSpan - tokenOffset; j++)
					*(_outstr++) = str[tokenStart + tokenOffset + j];
				*(_outstr++) = ']';
			} else {
				for (uint16 j = 0; j < keybufSize; j++)
					*(_outstr++) = keybuf[j];
			}
#else
			i += GetTokenPadKeyString(&str[i], keybuf) + 1;
			uint16 keybuf_size = GetWideStringLength(keybuf);
			for (uint16 j = 0; j < keybuf_size; j++) {
				*(_outstr++) = keybuf[j];
				keybuf[j] = '\0';
			}
#endif

		} else {
			*(_outstr++) = str[i++];
		}
	}
	*_outstr = '\0';

	for (i = 0; i < GetWideStringLength(outstr); i++)
		str[i] = outstr[i];

	while (i < 256)
		str[i++] = '\0';
}

void
CMessages::AddMessageWithNumber(wchar *str, uint32 time, uint16 flag, int32 n1, int32 n2, int32 n3, int32 n4, int32 n5, int32 n6)
{
	wchar outstr[512]; // unused
	InsertNumberInString(str, n1, n2, n3, n4, n5, n6, outstr);
	InsertPlayerControlKeysInString(outstr);
	GetWideStringLength(outstr);

	uint16 i = 0;
	while (i < NUMBRIEFMESSAGES && BriefMessages[i].m_pText != nil)
		i++;

	if (i >= NUMBRIEFMESSAGES) return;

	BriefMessages[i].m_pText = str;
	BriefMessages[i].m_nFlag = flag;
	BriefMessages[i].m_nTime = time;
	BriefMessages[i].m_nStartTime = CTimer::GetTimeInMilliseconds();
	BriefMessages[i].m_nNumber[0] = n1;
	BriefMessages[i].m_nNumber[1] = n2;
	BriefMessages[i].m_nNumber[2] = n3;
	BriefMessages[i].m_nNumber[3] = n4;
	BriefMessages[i].m_nNumber[4] = n5;
	BriefMessages[i].m_nNumber[5] = n6;
	BriefMessages[i].m_pString = nil;
	if (i == 0)
		AddToPreviousBriefArray(
			BriefMessages[0].m_pText,
			BriefMessages[0].m_nNumber[0],
			BriefMessages[0].m_nNumber[1],
			BriefMessages[0].m_nNumber[2],
			BriefMessages[0].m_nNumber[3],
			BriefMessages[0].m_nNumber[4],
			BriefMessages[0].m_nNumber[5],
			BriefMessages[0].m_pString);
}

void 
CMessages::AddMessageJumpQWithNumber(wchar *str, uint32 time, uint16 flag, int32 n1, int32 n2, int32 n3, int32 n4, int32 n5, int32 n6)
{
	wchar outstr[512]; // unused
	InsertNumberInString(str, n1, n2, n3, n4, n5, n6, outstr);
	InsertPlayerControlKeysInString(outstr);
	GetWideStringLength(outstr);

	BriefMessages[0].m_pText = str;
	BriefMessages[0].m_nFlag = flag;
	BriefMessages[0].m_nTime = time;
	BriefMessages[0].m_nStartTime = CTimer::GetTimeInMilliseconds();
	BriefMessages[0].m_nNumber[0] = n1;
	BriefMessages[0].m_nNumber[1] = n2;
	BriefMessages[0].m_nNumber[2] = n3;
	BriefMessages[0].m_nNumber[3] = n4;
	BriefMessages[0].m_nNumber[4] = n5;
	BriefMessages[0].m_nNumber[5] = n6;
	BriefMessages[0].m_pString = nil;
	AddToPreviousBriefArray(str, n1, n2, n3, n4, n5, n6, nil);
}

void
CMessages::AddMessageSoonWithNumber(wchar *str, uint32 time, uint16 flag, int32 n1, int32 n2, int32 n3, int32 n4, int32 n5, int32 n6)
{
	wchar outstr[512]; // unused
	InsertNumberInString(str, n1, n2, n3, n4, n5, n6, outstr);
	InsertPlayerControlKeysInString(outstr);
	GetWideStringLength(outstr);

	if (BriefMessages[0].m_pText != nil) {
		for (int32 i = NUMBRIEFMESSAGES-1; i > 1; i--)
			BriefMessages[i] = BriefMessages[i-1];

		BriefMessages[1].m_pText = str;
		BriefMessages[1].m_nFlag = flag;
		BriefMessages[1].m_nTime = time;
		BriefMessages[1].m_nStartTime = CTimer::GetTimeInMilliseconds();
		BriefMessages[1].m_nNumber[0] = n1;
		BriefMessages[1].m_nNumber[1] = n2;
		BriefMessages[1].m_nNumber[2] = n3;
		BriefMessages[1].m_nNumber[3] = n4;
		BriefMessages[1].m_nNumber[4] = n5;
		BriefMessages[1].m_nNumber[5] = n6;
		BriefMessages[1].m_pString = nil;
	} else {
		BriefMessages[0].m_pText = str;
		BriefMessages[0].m_nFlag = flag;
		BriefMessages[0].m_nTime = time;
		BriefMessages[0].m_nStartTime = CTimer::GetTimeInMilliseconds();
		BriefMessages[0].m_nNumber[0] = n1;
		BriefMessages[0].m_nNumber[1] = n2;
		BriefMessages[0].m_nNumber[2] = n3;
		BriefMessages[0].m_nNumber[3] = n4;
		BriefMessages[0].m_nNumber[4] = n5;
		BriefMessages[0].m_nNumber[5] = n6;
		BriefMessages[0].m_pString = nil;
		AddToPreviousBriefArray(str, n1, n2, n3, n4, n5, n6, nil);
	}
}

void
CMessages::AddBigMessageWithNumber(wchar *str, uint32 time, uint16 style, int32 n1, int32 n2, int32 n3, int32 n4, int32 n5, int32 n6)
{
	time = GetBigMessageTime(str, time, style);
	if (style == 1)
		MissionTitleWaitPending = true;
	wchar outstr[512]; // unused
	InsertNumberInString(str, n1, n2, n3, n4, n5, n6, outstr);
	InsertPlayerControlKeysInString(outstr);
	GetWideStringLength(outstr);

	BIGMessages[style].m_Stack[0].m_pText = str;
	BIGMessages[style].m_Stack[0].m_nFlag = 0;
	BIGMessages[style].m_Stack[0].m_nTime = time;
	BIGMessages[style].m_Stack[0].m_nStartTime = CTimer::GetTimeInMilliseconds();
	BIGMessages[style].m_Stack[0].m_nNumber[0] = n1;
	BIGMessages[style].m_Stack[0].m_nNumber[1] = n2;
	BIGMessages[style].m_Stack[0].m_nNumber[2] = n3;
	BIGMessages[style].m_Stack[0].m_nNumber[3] = n4;
	BIGMessages[style].m_Stack[0].m_nNumber[4] = n5;
	BIGMessages[style].m_Stack[0].m_nNumber[5] = n6;
	BIGMessages[style].m_Stack[0].m_pString = nil;
}

void
CMessages::AddBigMessageWithNumberQ(wchar *str, uint32 time, uint16 style, int32 n1, int32 n2, int32 n3, int32 n4, int32 n5, int32 n6)
{
	time = GetBigMessageTime(str, time, style);
	if (style == 1)
		MissionTitleWaitPending = true;
	wchar outstr[512]; // unused
	InsertNumberInString(str, n1, n2, n3, n4, n5, n6, outstr);
	InsertPlayerControlKeysInString(outstr);
	GetWideStringLength(outstr);

	int32 i = 0;

	while (i < 4 && BIGMessages[style].m_Stack[i].m_pText != nil)
		i++;

	if (i >= 4) return;

	BIGMessages[style].m_Stack[i].m_pText = str;
	BIGMessages[style].m_Stack[i].m_nFlag = 0;
	BIGMessages[style].m_Stack[i].m_nTime = time;
	BIGMessages[style].m_Stack[i].m_nStartTime = CTimer::GetTimeInMilliseconds();
	BIGMessages[style].m_Stack[i].m_nNumber[0] = n1;
	BIGMessages[style].m_Stack[i].m_nNumber[1] = n2;
	BIGMessages[style].m_Stack[i].m_nNumber[2] = n3;
	BIGMessages[style].m_Stack[i].m_nNumber[3] = n4;
	BIGMessages[style].m_Stack[i].m_nNumber[4] = n5;
	BIGMessages[style].m_Stack[i].m_nNumber[5] = n6;
	BIGMessages[style].m_Stack[i].m_pString = nil;
}

void
CMessages::AddMessageWithString(wchar *text, uint32 time, uint16 flag, wchar *str)
{
	wchar outstr[512]; // unused
	WideStringCopy(outstr, text, 256);
	InsertStringInString(outstr, str);
	InsertPlayerControlKeysInString(outstr);
	GetWideStringLength(outstr);

	int32 i = 0;
	while (i < NUMBRIEFMESSAGES && BriefMessages[i].m_pText != nil)
		i++;

	if (i >= NUMBRIEFMESSAGES) return;

	BriefMessages[i].m_pText = text;
	BriefMessages[i].m_nFlag = flag;
	BriefMessages[i].m_nTime = time;
	BriefMessages[i].m_nStartTime = CTimer::GetTimeInMilliseconds();
	BriefMessages[i].m_nNumber[0] = -1;
	BriefMessages[i].m_nNumber[1] = -1;
	BriefMessages[i].m_nNumber[2] = -1;
	BriefMessages[i].m_nNumber[3] = -1;
	BriefMessages[i].m_nNumber[4] = -1;
	BriefMessages[i].m_nNumber[5] = -1;
	BriefMessages[i].m_pString = str;
	if (i == 0)
		AddToPreviousBriefArray(
			BriefMessages[0].m_pText,
			BriefMessages[0].m_nNumber[0],
			BriefMessages[0].m_nNumber[1],
			BriefMessages[0].m_nNumber[2],
			BriefMessages[0].m_nNumber[3],
			BriefMessages[0].m_nNumber[4],
			BriefMessages[0].m_nNumber[5],
			BriefMessages[0].m_pString);
}

void
CMessages::AddMessageJumpQWithString(wchar *text, uint32 time, uint16 flag, wchar *str)
{
	wchar outstr[512]; // unused
	WideStringCopy(outstr, text, 256);
	InsertStringInString(outstr, str);
	InsertPlayerControlKeysInString(outstr);
	GetWideStringLength(outstr);

	BriefMessages[0].m_pText = text;
	BriefMessages[0].m_nFlag = flag;
	BriefMessages[0].m_nTime = time;
	BriefMessages[0].m_nStartTime = CTimer::GetTimeInMilliseconds();
	BriefMessages[0].m_nNumber[0] = -1;
	BriefMessages[0].m_nNumber[1] = -1;
	BriefMessages[0].m_nNumber[2] = -1;
	BriefMessages[0].m_nNumber[3] = -1;
	BriefMessages[0].m_nNumber[4] = -1;
	BriefMessages[0].m_nNumber[5] = -1;
	BriefMessages[0].m_pString = str;
	AddToPreviousBriefArray(text, -1, -1, -1, -1, -1, -1, str);
}

inline bool
FastWideStringComparison(wchar *str1, wchar *str2)
{
	while (*str1 == *str2) {
		++str1;
		++str2;
		if (!*str1 && !*str2) return true;
	}
	return false;
}

void
CMessages::ClearThisPrint(wchar *str)
{
	bool equal;

	do {
		equal = false;
		uint16 i;
		for (i = 0; i < NUMBRIEFMESSAGES && BriefMessages[i].m_pText != nil; i++) {
			equal = FastWideStringComparison(str, BriefMessages[i].m_pText);

			if (equal) break;
		}

		if (equal) {
			if (i != 0) {
				BriefMessages[i].m_pText = nil;
				for (; i < NUMBRIEFMESSAGES-1 && BriefMessages[i+1].m_pText != nil; i++) {
					BriefMessages[i] = BriefMessages[i + 1];
				}
				BriefMessages[i].m_pText = nil;
			} else {
				BriefMessages[0].m_pText = nil;
				for (; i < NUMBRIEFMESSAGES-1 && BriefMessages[i+1].m_pText != nil; i++) {
					BriefMessages[i] = BriefMessages[i + 1];
				}
				BriefMessages[i].m_pText = nil;
				BriefMessages[0].m_nStartTime = CTimer::GetTimeInMilliseconds();
				if (BriefMessages[0].m_pText != nil)
					AddToPreviousBriefArray(
						BriefMessages[0].m_pText,
						BriefMessages[0].m_nNumber[0],
						BriefMessages[0].m_nNumber[1],
						BriefMessages[0].m_nNumber[2],
						BriefMessages[0].m_nNumber[3],
						BriefMessages[0].m_nNumber[4],
						BriefMessages[0].m_nNumber[5],
						BriefMessages[0].m_pString);
			}
		}
	} while (equal);
}

void
CMessages::ClearThisBigPrint(wchar *str)
{
	bool equal;

	do {
		uint16 i = 0;
		equal = false;
		uint16 style = 0;
		while (style < NUMBIGMESSAGES)
		{
			if (i >= 4)
				break;

			if (CMessages::BIGMessages[style].m_Stack[i].m_pText == nil || equal)
				break;

			equal = FastWideStringComparison(str, BIGMessages[style].m_Stack[i].m_pText);

			if (!equal && ++i == 4) {
				i = 0;
				style++;
			}
		}
		if (equal) {
			if (i != 0) {
				BIGMessages[style].m_Stack[i].m_pText = nil;
				while (i < 3) {
					if (BIGMessages[style].m_Stack[i + 1].m_pText == nil)
						break;
					BIGMessages[style].m_Stack[i] = BIGMessages[style].m_Stack[i + 1];
					i++;
				}
				BIGMessages[style].m_Stack[i].m_pText = nil;
			} else {
				BIGMessages[style].m_Stack[0].m_pText = nil;
				i = 0;
				while (i < 3) {
					if (BIGMessages[style].m_Stack[i + 1].m_pText == nil)
						break;
					BIGMessages[style].m_Stack[i] = BIGMessages[style].m_Stack[i + 1];
					i++;
				}
				BIGMessages[style].m_Stack[i].m_pText = nil;
				BIGMessages[style].m_Stack[0].m_nStartTime = CTimer::GetTimeInMilliseconds();
			}
		}
	} while (equal);
}

void
CMessages::ClearAllMessagesDisplayedByGame()
{
	ClearMessages();
	for (int32 i = 0; i < NUMPREVIOUSBRIEFS; i++) {
		PreviousBriefs[i].m_pText = nil;
		PreviousBriefs[i].m_pString = nil;
	}
	CHud::GetRidOfAllHudMessages();
	CUserDisplay::Pager.ClearMessages();
}

void
CMessages::ClearThisBigPrintNow(uint32 id)
{
	if (BIGMessages[id].m_Stack[0].m_pText)
		ClearThisBigPrint(BIGMessages[id].m_Stack[0].m_pText);
	CHud::m_BigMessage[id][0] = '\0';
	BigMessageInUse[id] = 0.0f;
}
