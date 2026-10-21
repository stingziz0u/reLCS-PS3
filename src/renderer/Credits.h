#pragma once

class CCredits
{
	static bool bCreditsGoing;
	static uint32 CreditsStartTime;
public:
	static void Init(void);
	static void Start(int32 group = 0);
	static void Stop(void);
	static bool AreCreditsDone(void);
	static void Render(void);
	static void PrintCreditSpace(float space, float &line);
	static void PrintCreditText(float scaleX, float scaleY, wchar *text, float &lineoffset, float scrolloffset);
};
