/* SPDX-License-Identifier: GPL-3.0-or-later
 * Capture actual Windows XP GDI monochrome glyphs, without FreeType/Wine.
 * Built without a CRT; uses APIs shipped in Windows XP. */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501
#include <windows.h>
DWORD _tls_index=0;
static BYTE bitmap[262144];
static HANDLE output;
static BOOL write_bytes(const void *data,DWORD size) {
 DWORD done=0;return WriteFile(output,data,size,&done,0)&&done==size;
}
static BOOL capture(HDC dc,LONG weight,const WCHAR *path,OSVERSIONINFOW *os) {
 HFONT font=CreateFontW(-11,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
  OUT_TT_ONLY_PRECIS,CLIP_DEFAULT_PRECIS,NONANTIALIASED_QUALITY,DEFAULT_PITCH,L"Tahoma");
 if(!font)return FALSE;
 HGDIOBJ old=SelectObject(dc,font);
 WCHAR actual[64];GetTextFaceW(dc,64,actual);
 if(actual[0]!='T'||actual[1]!='a'||actual[2]!='h'||actual[3]!='o'||actual[4]!='m'||actual[5]!='a'||actual[6])return FALSE;
 TEXTMETRICW tm;if(!GetTextMetricsW(dc,&tm))return FALSE;
 output=CreateFileW(path,GENERIC_WRITE,0,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
 if(output==INVALID_HANDLE_VALUE)return FALSE;
 DWORD header[]={1,os->dwMajorVersion,os->dwMinorVersion,os->dwBuildNumber,11,(DWORD)weight,(DWORD)tm.tmAscent,(DWORD)tm.tmDescent};
 BOOL ok=write_bytes("XPGDI001",8)&&write_bytes(header,sizeof(header));
 MAT2 matrix={ {0,1},{0,0},{0,0},{0,1} };
 for(DWORD code=0;ok&&code<65536;code++) {
  if(code>=0xd800&&code<=0xdfff)continue;
  WCHAR ch=(WCHAR)code;WORD glyph=0xffff;
  if(GetGlyphIndicesW(dc,&ch,1,&glyph,GGI_MARK_NONEXISTING_GLYPHS)==GDI_ERROR){ok=FALSE;break;}
  if(glyph==0xffff)continue;
  GLYPHMETRICS gm;
  DWORD size=GetGlyphOutlineW(dc,glyph,GGO_BITMAP|GGO_GLYPH_INDEX,&gm,0,0,&matrix);
  if(size==GDI_ERROR||size>sizeof(bitmap)){ok=FALSE;break;}
  if(size&&GetGlyphOutlineW(dc,glyph,GGO_BITMAP|GGO_GLYPH_INDEX,&gm,size,bitmap,&matrix)==GDI_ERROR){ok=FALSE;break;}
  LONG record[]={ (LONG)code,glyph,(LONG)gm.gmBlackBoxX,(LONG)gm.gmBlackBoxY,
   gm.gmptGlyphOrigin.x,gm.gmptGlyphOrigin.y,gm.gmCellIncX,gm.gmCellIncY,(LONG)size };
  ok=write_bytes(record,sizeof(record))&&write_bytes(bitmap,size);
 }
 DWORD end=0xffffffff;if(ok)ok=write_bytes(&end,4);
 FlushFileBuffers(output);CloseHandle(output);SelectObject(dc,old);DeleteObject(font);return ok;
}
void WINAPI mainCRTStartup(void) {
 OSVERSIONINFOW os={0};os.dwOSVersionInfoSize=sizeof(os);
 if(!GetVersionExW(&os)||os.dwMajorVersion!=5||os.dwMinorVersion!=1){
  MessageBoxW(0,L"Run this capture utility inside Windows XP (5.1).",L"XP Tahoma capture",MB_OK|MB_ICONERROR);ExitProcess(1);
 }
 CreateDirectoryW(L"C:\\XPFONT",0);HDC dc=CreateCompatibleDC(0);
 BOOL ok=dc&&capture(dc,400,L"C:\\XPFONT\\regular.xpg",&os)&&capture(dc,700,L"C:\\XPFONT\\bold.xpg",&os);
 if(dc)DeleteDC(dc);
 MessageBoxW(0,ok?L"Captured regular and bold Tahoma using XP GDI. Files: C:\\XPFONT":L"Capture failed. Incomplete files must not be used.",L"XP Tahoma capture",MB_OK|(ok?MB_ICONINFORMATION:MB_ICONERROR));
 ExitProcess(ok?0:1);
}
