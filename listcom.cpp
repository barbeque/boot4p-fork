/*
 Enumerating com ports in Windows 

 original by PJ Naughter, 1998 - 2013 
 http://www.naughter.com/enumser.html  (Web: www.naughter.com, Email: pjna@naughter.com)

 reorganize by Gaiger Chen , Jul, 2015
 http://gaiger-programming.blogspot.tw/2015/07/methods-collection-of-enumerating-com.html

 NO COPYRIGHT,  welcome to use for everyone.
*/

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <setupapi.h>

#pragma comment(lib, "advapi32")

// Hack to allow compilation under gcc
#if !defined __tcscat_s
#define _tcscat_s(dst, size, src) _tcscat(dst, src)
#endif

BOOL EnumerateComPortSetupAPISetupDiClassGuidsFromNamePort(UINT *pNumber, 
  TCHAR *pPortName, int strMaxLen, TCHAR *pFriendName);

int listcom(bool print)
{
	static TCHAR portname[256 * 256], friendname[256 * 256];
	UINT n;

	BOOL ret = EnumerateComPortSetupAPISetupDiClassGuidsFromNamePort(&n, portname, 256,
		friendname);

	if (!ret || n == 0) {
		if (print)
			printf("No COM ports found\n");
		return 0;
	}

	if (print) {
		printf("Available COM ports:\n");
		for (UINT i = 0; i < n; i++) {
			printf("\t%s %s\n", portname + i * 256, friendname + i * 256);
		}
	}

	int defport = 0;
	for (UINT i = 0; i < n; i++) {
		TCHAR *name = portname + i * 256;
		for (; *name && (*name < '0' || *name > '9'); name++);
		defport = atoi(name);
	}

	return defport;
}

BOOL EnumerateComPortSetupAPISetupDiClassGuidsFromNamePort(UINT *pNumber, 
  TCHAR *pPortName, int strMaxLen, TCHAR *pFriendName)
{ 
 UINT i, jj;
 INT ret;
 
 TCHAR *pTempPortName;
 HMODULE hLibrary;
 TCHAR szFullPath[_MAX_PATH];

 GUID *pGuid;
 DWORD dwGuids;
 HDEVINFO hDevInfoSet;
  

 typedef HKEY (__stdcall SetupDiOpenDevRegKeyFunType)
  (HDEVINFO, PSP_DEVINFO_DATA, DWORD, DWORD, DWORD, REGSAM);
 
 typedef BOOL (__stdcall SetupDiClassGuidsFromNameFunType)
  (LPCTSTR, LPGUID, DWORD, PDWORD);

 typedef BOOL (__stdcall SetupDiDestroyDeviceInfoListFunType)
  (HDEVINFO);
 typedef BOOL (__stdcall SetupDiEnumDeviceInfoFunType)
  (HDEVINFO, DWORD, PSP_DEVINFO_DATA);

 typedef HDEVINFO (__stdcall SetupDiGetClassDevsFunType)
  (LPGUID, LPCTSTR, HWND, DWORD);

 typedef BOOL (__stdcall SetupDiGetDeviceRegistryPropertyFunType)
  (HDEVINFO, PSP_DEVINFO_DATA, DWORD, PDWORD, PBYTE, DWORD, PDWORD);

 SetupDiOpenDevRegKeyFunType* SetupDiOpenDevRegKeyFunPtr;

 SetupDiClassGuidsFromNameFunType *SetupDiClassGuidsFromNameFunPtr;
 SetupDiGetClassDevsFunType *SetupDiGetClassDevsFunPtr;
 SetupDiGetDeviceRegistryPropertyFunType *SetupDiGetDeviceRegistryPropertyFunPtr; 

 SetupDiDestroyDeviceInfoListFunType *SetupDiDestroyDeviceInfoListFunPtr;  
 SetupDiEnumDeviceInfoFunType *SetupDiEnumDeviceInfoFunPtr; 

 BOOL bMoreItems;
 SP_DEVINFO_DATA devInfo;

 ret = FALSE;
 jj = 0;
 szFullPath[0] = _T('\0'); 

 //Get the Windows System32 directory
 
 
 if(0 == GetSystemDirectory(szFullPath, _countof(szFullPath)))
 {
  _tprintf(TEXT("CEnumerateSerial::UsingSetupAPI1 failed, Error:%u\n"), 
   GetLastError());
  return FALSE;
 }/*if*/
 

 //Setup the full path and delegate to LoadLibrary    
#pragma warning(suppress: 6102) //There is a bug with the SAL annotation of GetSystemDirectory in the Windows 8.1 SDK
 _tcscat_s(szFullPath, _countof(szFullPath), _T("\\"));
 _tcscat_s(szFullPath, _countof(szFullPath), TEXT("SETUPAPI.DLL"));
 hLibrary = LoadLibrary(szFullPath);

 
 SetupDiOpenDevRegKeyFunPtr = 
  (SetupDiOpenDevRegKeyFunType*)GetProcAddress(hLibrary, "SetupDiOpenDevRegKey");
  
#if defined _UNICODE 
 SetupDiClassGuidsFromNameFunPtr = (SetupDiClassGuidsFromNameFunType*)
  GetProcAddress(hLibrary, "SetupDiGetDeviceRegistryPropertyW");
 SetupDiGetClassDevsFunPtr = 
  (SetupDiGetClassDevsFunType*)GetProcAddress(hLibrary, "SetupDiGetClassDevsW");
 SetupDiGetDeviceRegistryPropertyFunPtr
  = (SetupDiGetDeviceRegistryPropertyFunType*)GetProcAddress(hLibrary, "SetupDiGetDeviceRegistryPropertyW");
#else
 SetupDiClassGuidsFromNameFunPtr = (SetupDiClassGuidsFromNameFunType*)
  GetProcAddress(hLibrary, "SetupDiClassGuidsFromNameA");
 SetupDiGetClassDevsFunPtr = (SetupDiGetClassDevsFunType*)
  GetProcAddress(hLibrary, "SetupDiGetClassDevsA");
 SetupDiGetDeviceRegistryPropertyFunPtr = (SetupDiGetDeviceRegistryPropertyFunType*)
  GetProcAddress(hLibrary, "SetupDiGetDeviceRegistryPropertyA");
#endif

  SetupDiDestroyDeviceInfoListFunPtr = (SetupDiDestroyDeviceInfoListFunType*)
  GetProcAddress(hLibrary, "SetupDiDestroyDeviceInfoList");

 SetupDiEnumDeviceInfoFunPtr = (SetupDiEnumDeviceInfoFunType*)
  GetProcAddress(hLibrary, "SetupDiEnumDeviceInfo");

 
 //First need to convert the name "Ports" to a GUID using SetupDiClassGuidsFromName
 dwGuids = 0;
 SetupDiClassGuidsFromNameFunPtr(TEXT("Ports"), NULL, 0, &dwGuids);

 if(0 == dwGuids)  
  return FALSE;
 
 //Allocate the needed memory
 pGuid = (GUID*)HeapAlloc(GetProcessHeap(), 
  HEAP_GENERATE_EXCEPTIONS, dwGuids * sizeof(GUID));
 
 if(NULL == pGuid) 
  return FALSE;
 

 //Call the function again
 
 if (FALSE == SetupDiClassGuidsFromNameFunPtr(TEXT("Ports"), 
   pGuid, dwGuids, &dwGuids))  
 {
  return FALSE;
 }/*if*/


 hDevInfoSet = SetupDiGetClassDevsFunPtr(pGuid, NULL, NULL, 
  DIGCF_PRESENT /*| DIGCF_DEVICEINTERFACE*/);


 if (INVALID_HANDLE_VALUE == hDevInfoSet)
 {
  //Set the error to report
  _tprintf(TEXT("error SetupDiGetClassDevsFunPtr, %d"), GetLastError());
  return FALSE;
 }/*if */
 

 //bMoreItems = TRUE;
 devInfo.cbSize = sizeof(SP_DEVINFO_DATA);
 i = 0;
 jj = 0;

 do
 {
  HKEY hDeviceKey;  
  BOOL isFound;

  isFound = FALSE;
  bMoreItems = SetupDiEnumDeviceInfoFunPtr(hDevInfoSet, i, &devInfo);  
  if(FALSE == bMoreItems)
   break;   

  i++;

  hDeviceKey = SetupDiOpenDevRegKeyFunPtr(hDevInfoSet, &devInfo, 
   DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
 
  if (INVALID_HANDLE_VALUE != hDeviceKey)
  {
   int nPort;    
   size_t nLen;
   LPTSTR pszPortName;

   nPort = 0;
   pszPortName = NULL;

   {    
    //First query for the size of the registry value 
    DWORD dwType;
    DWORD dwDataSize;
    LONG err;
    DWORD dwAllocatedSize;
    DWORD dwReturnedSize;
    dwType = 0; dwDataSize = 0; 

    err = RegQueryValueEx(hDeviceKey, TEXT("PortName"), NULL, 
     &dwType, NULL, &dwDataSize);

    if (ERROR_SUCCESS != err)    
     continue;    
    
    //Ensure the value is a string
    if (dwType != REG_SZ)    
     continue;
    
    //Allocate enough bytes for the return value
    dwAllocatedSize = dwDataSize + sizeof(TCHAR);

    /* +sizeof(TCHAR) is to allow us to NULL terminate 
     the data if it is not null terminated in the registry
    */

    pszPortName = (LPTSTR)LocalAlloc(LMEM_FIXED, dwAllocatedSize); 

    if (pszPortName == NULL)
     continue;

    //Recall RegQueryValueEx to return the data
    pszPortName[0] = TEXT('\0');
    dwReturnedSize = dwAllocatedSize;

    err = RegQueryValueEx(hDeviceKey, TEXT("PortName"), NULL, 
     &dwType, (LPBYTE)pszPortName, &dwReturnedSize);

    if (ERROR_SUCCESS != err)
    {
     LocalFree(pszPortName);
     pszPortName = NULL;     
     continue;
    }

    //Handle the case where the data just returned is the same size as the allocated size. This could occur where the data
    //has been updated in the registry with a non null terminator between the two calls to ReqQueryValueEx above. Rather than
    //return a potentially non-null terminated block of data, just fail the method call
    if (dwReturnedSize >= dwAllocatedSize)
     continue;        

    //NULL terminate the data if it was not returned NULL terminated because it is not stored null terminated in the registry
    if (pszPortName[dwReturnedSize/sizeof(TCHAR) - 1] != _T('\0'))
     pszPortName[dwReturnedSize/sizeof(TCHAR)] = _T('\0');
   }/*local varable*/

   //If it looks like "COMX" then
   //add it to the array which will be returned
   nLen = _tcslen(pszPortName);

   if (3 < nLen)
   {
     if (0 == _tcsnicmp(pszPortName, TEXT("COM"), 3))
     {
     if(FALSE == isdigit(pszPortName[3]) )
      continue;

    //Work out the port number
     _tcsncpy(pPortName + jj*strMaxLen, pszPortName, 
     _tcsnlen(pszPortName, strMaxLen));

    //_stprintf_s(&portName[jj][0], strMaxLen, TEXT("%s"), pszPortName);          
     }
     else
     {
      continue;
     }/*if 0 == _tcsnicmp(pszPortName, TEXT("COM"), 3)*/

   }/*if 3 < nLen*/

   LocalFree(pszPortName);    
   isFound = TRUE;  

   //Close the key now that we are finished with it
   RegCloseKey(hDeviceKey);
  }/*INVALID_HANDLE_VALUE != hDeviceKey*/

  if(FALSE == isFound)
   continue;

  //If the port was a serial port, then also try to get its friendly name  
  {
   TCHAR szFriendlyName[1024];     
   DWORD dwSize;
   DWORD dwType;
   szFriendlyName[0] = _T('\0');
   dwSize = sizeof(szFriendlyName);
   dwType = 0;

   if( (TRUE == SetupDiGetDeviceRegistryPropertyFunPtr(hDevInfoSet, &devInfo, 
       SPDRP_DEVICEDESC, &dwType, (PBYTE)(szFriendlyName), 
       dwSize, &dwSize) ) && (REG_SZ == dwType)        
   )
   {
    _tcsncpy(pFriendName + jj*strMaxLen, &szFriendlyName[0], 
     _tcsnlen(&szFriendlyName[0], strMaxLen));
   }
   else
   {
    //_stprintf_s(pFriendName + jj*strMaxLen, strMaxLen, TEXT("")); 
    _tcsncpy(pFriendName + jj*strMaxLen, TEXT(""), 
     _tcsnlen(TEXT(""), strMaxLen));
   }/*if SetupDiGetDeviceRegistryPropertyFunPtr */    
  }/*local variable */

  jj++;
 }while(1);

 HeapFree(GetProcessHeap(), 0, pGuid);

 *pNumber = jj;

 if(0 <jj)
  ret = TRUE;

 return ret;
}/*EnumerateComPortSetupAPISetupDiClassGuidsFromNamePort*/

#endif // _WIN32