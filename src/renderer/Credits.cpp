#include "common.h"

#include "Timer.h"
#include "Font.h"
#include "Frontend.h"
#include "RwHelper.h"
#include "Camera.h"
#include "Text.h"
#include "Credits.h"
#include "Pad.h"

bool CCredits::bCreditsGoing;
uint32 CCredits::CreditsStartTime;

static const uint8 CreditsEntriesPerSection[] = {
	38, 26, 14, 30, 17, 11, 11, 9, 19, 11,
	12, 24, 15, 9, 9, 4, 12, 8, 20
};

void
CCredits::Init(void)
{
	Stop();
}

void
CCredits::Start(int32 group)
{
	/* LCS stores its complete credits in the CRED01 mission-text table.
	 * START_CREDITS passes group 0; the group is not a table number. */
	(void)group;
	char table[] = "CRED01";
	TheText.LoadMissionText(table);
	bCreditsGoing = true;
	CreditsStartTime = CTimer::GetTimeInMilliseconds();
}

void
CCredits::Stop(void)
{
	bCreditsGoing = false;
}

void
CCredits::PrintCreditSpace(float space, float &line)
{
	line += space * 25.0f;
}

void
CCredits::PrintCreditText(float scaleX, float scaleY, wchar *text, float &lineoffset, float scrolloffset)
{
	if (text == nil || text[0] == '\0')
		return;

	CFont::SetScale(SCREEN_SCALE_X(scaleX), SCREEN_SCALE_Y(scaleY));
	const int32 lines = Max(CFont::GetNumberLines(SCREEN_WIDTH / 2.0f, 0.0f, text), 1);
	const float blockHeight = lines * scaleY * 25.0f;
	const float start = DEFAULT_SCREEN_HEIGHT + 20.0f;
	const float y = lineoffset + start - scrolloffset;
	if (y + blockHeight > 20.0f && y < DEFAULT_SCREEN_HEIGHT - 20.0f) {
		CFont::SetColor(CRGBA(0, 0, 0, 255));
		CFont::PrintString(SCREEN_WIDTH / 2.0f, SCREEN_SCALE_Y(y), (uint16*)text);
		CFont::SetColor(CRGBA(220, 220, 220, 220));
		CFont::PrintString(SCREEN_WIDTH / 2.0f - SCREEN_SCALE_X(1.0f), SCREEN_SCALE_Y(y - 1.0f), (uint16*)text);
	}
	lineoffset += blockHeight;
}

void
CCredits::Render(void)
{
	if (!bCreditsGoing || FrontEndMenuManager.m_bMenuActive)
		return;

	CPad::UpdatePads();
	if (CPad::GetPad(0)->GetCrossJustDown()) {
		bCreditsGoing = false;
		return;
	}

	DefinedState();
	float lineoffset = 0.0f;
	const float scrolloffset = (CTimer::GetTimeInMilliseconds() - CreditsStartTime) / 24.0f;
	CFont::SetJustifyOff();
	CFont::SetBackgroundOff();
	CFont::SetCentreSize(SCREEN_SCALE_X(DEFAULT_SCREEN_WIDTH * 0.75f));
	CFont::SetCentreOn();
	CFont::SetPropOn();
	CFont::SetFontStyle(FONT_STANDARD);

	char key[8];
	for (int32 section = 0; section < ARRAY_SIZE(CreditsEntriesPerSection); section++) {
		for (int32 entry = 1; entry <= CreditsEntriesPerSection[section]; entry++) {
			snprintf(key, sizeof(key), "CR%02d_%02d", section + 1, entry);
			PrintCreditText(0.82f, 0.82f, TheText.Get(key), lineoffset, scrolloffset);
		}
		PrintCreditSpace(0.75f, lineoffset);
	}

	CFont::DrawFonts();
#ifdef CUTSCENE_BORDERS_SWITCH
	if (CMenuManager::m_PrefsCutsceneBorders)
#endif
	if (TheCamera.m_WideScreenOn)
		TheCamera.DrawBordersForWideScreen();

	if (lineoffset + DEFAULT_SCREEN_HEIGHT - scrolloffset < -10.0f)
		bCreditsGoing = false;
}

bool
CCredits::AreCreditsDone(void)
{
	return !bCreditsGoing;
}
