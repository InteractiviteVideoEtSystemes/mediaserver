#ifndef _LOGO_H_
#define _LOGO_H_
#include "config.h"

class Logo
{
public:
	Logo();
	~Logo();
	Logo & operator =(const Logo& l);
	int Load(const char *filename);
	void Clean();
	
	BYTE* GetFrame();
	int GetWidth();
	int GetHeight();

private:
	BYTE*	 frame;
	int width;
	int height;
};

#endif
