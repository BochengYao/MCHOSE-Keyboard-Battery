// Minimal public Win32 ABI declarations; only OS-cached standard BAS values are read.
// Reference: Microsoft's bthledef.h and BluetoothGATTGet* documentation.
typedef struct { BOOLEAN shortUuid;union {USHORT shortValue;GUID longValue;} value; } LEUuid;
typedef struct { LEUuid uuid;USHORT handle; } LEService;
typedef struct { USHORT service;LEUuid uuid;USHORT handle,valueHandle;BOOLEAN broadcast,read,write,writeNoResponse,signedWrite,notify,indicate,extended; } LECharacteristic;
typedef struct { ULONG size;BYTE data[1]; } LEValue;
typedef HRESULT (WINAPI *GattServicesFn)(HANDLE,USHORT,LEService*,USHORT*,ULONG);
typedef HRESULT (WINAPI *GattCharsFn)(HANDLE,LEService*,USHORT,LECharacteristic*,USHORT*,ULONG);
typedef HRESULT (WINAPI *GattValueFn)(HANDLE,LECharacteristic*,ULONG,LEValue*,USHORT*,ULONG);
static BOOL UuidIs(const LEUuid *uuid,USHORT value) {
    if(uuid->shortUuid) return uuid->value.shortValue==value;
    GUID full={value,0,0x1000,{0x80,0,0,0x80,0x5f,0x9b,0x34,0xfb}};return memcmp(&full,&uuid->value.longValue,sizeof(full))==0;
}
static Reading QueryGattCache(void) {
    Reading r={0};GUID guid={0x781aee18,0x7733,0x4ce4,{0xad,0xd0,0x91,0xf4,0x1c,0x67,0xb5,0x92}};
    HMODULE dll=LoadLibraryW(L"BluetoothApis.dll");if(!dll) return r;
    GattServicesFn getServices=(GattServicesFn)(void*)GetProcAddress(dll,"BluetoothGATTGetServices");
    GattCharsFn getChars=(GattCharsFn)(void*)GetProcAddress(dll,"BluetoothGATTGetCharacteristics");
    GattValueFn getValue=(GattValueFn)(void*)GetProcAddress(dll,"BluetoothGATTGetCharacteristicValue");
    if(!getServices || !getChars || !getValue) {FreeLibrary(dll);return r;}
    HDEVINFO set=SetupDiGetClassDevsW(&guid,NULL,NULL,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(set==INVALID_HANDLE_VALUE) {FreeLibrary(dll);return r;}
    for(DWORD i=0;i<32 && !r.ok;i++) {
        SP_DEVICE_INTERFACE_DATA di={0};di.cbSize=sizeof(di);SP_DEVINFO_DATA info={0};info.cbSize=sizeof(info);DWORD required=0,type=0;
        if(!SetupDiEnumDeviceInterfaces(set,NULL,&guid,i,&di)) break;
        SetupDiGetDeviceInterfaceDetailW(set,&di,NULL,0,&required,NULL);if(required<8 || required>16384) continue;
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,required);if(!detail) break;
        detail->cbSize=sizeof(*detail);wchar_t name[128]={0};
        if(!SetupDiGetDeviceInterfaceDetailW(set,&di,detail,required,NULL,&info)) {HeapFree(GetProcessHeap(),0,detail);continue;}
        if(!SetupDiGetDeviceRegistryPropertyW(set,&info,SPDRP_FRIENDLYNAME,&type,(BYTE*)name,sizeof(name),NULL)) SetupDiGetDeviceRegistryPropertyW(set,&info,SPDRP_DEVICEDESC,&type,(BYTE*)name,sizeof(name),NULL);
        if(wcsncmp(name,L"MCHOSE ",7)!=0 || !KnownFamily(name)) {HeapFree(GetProcessHeap(),0,detail);continue;}
        HANDLE h=CreateFileW(detail->DevicePath,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);HeapFree(GetProcessHeap(),0,detail);
        if(h==INVALID_HANDLE_VALUE) continue;
        LEService services[32];USHORT count=0;
        if(SUCCEEDED(getServices(h,32,services,&count,0)) && count<=32) for(USHORT s=0;s<count && !r.ok;s++) {
            if(!UuidIs(&services[s].uuid,0x180f)) continue;
            LECharacteristic chars[32];USHORT n=0;
            if(SUCCEEDED(getChars(h,&services[s],32,chars,&n,0)) && n<=32) for(USHORT j=0;j<n;j++) {
                if(!UuidIs(&chars[j].uuid,0x2a19) || !chars[j].read) continue;
                union {ULONG align;BYTE bytes[64];} storage={0};USHORT needed=0;LEValue *value=(LEValue*)storage.bytes;
                // FORCE_READ_FROM_CACHE avoids radio round trips, UI stalls, and stale-as-live claims.
                if(SUCCEEDED(getValue(h,&chars[j],sizeof(storage),value,&needed,0x08)) && value->size==1 && value->data[0]<=100) {
                    r.ok=TRUE;r.cached=TRUE;r.percent=value->data[0];r.protocol=6;wcsncpy(r.model,name+7,63);wcscpy(r.connection,L"蓝牙 GATT 缓存");break;
                }
            }
        }
        CloseHandle(h);
    }
    SetupDiDestroyDeviceInfoList(set);FreeLibrary(dll);return r;
}
