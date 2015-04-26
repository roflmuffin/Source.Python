/**
* =============================================================================
* Source Python
* Copyright (C) 2012 Source Python Development Team.  All rights reserved.
* =============================================================================
*
* This program is free software; you can redistribute it and/or modify it under
* the terms of the GNU General Public License, version 3.0, as published by the
* Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
* FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
* details.
*
* You should have received a copy of the GNU General Public License along with
* this program.  If not, see <http://www.gnu.org/licenses/>.
*
* As a special exception, the Source Python Team gives you permission
* to link the code of this program (as well as its derivative works) to
* "Half-Life 2," the "Source Engine," and any Game MODs that run on software
* by the Valve Corporation.  You must obey the GNU General Public License in
* all respects for all other code used.  Additionally, the Source.Python
* Development Team grants this exception to all derivative works.
*/

//-----------------------------------------------------------------------------
// Includes
//-----------------------------------------------------------------------------
#include <stdlib.h>
#include <string>

#include "dyncall.h"

#include "memory_hooks.h"
#include "memory_tools.h"
#include "utilities/wrap_macros.h"
#include "utilities/sp_util.h"
#include "utilities/call_python.h"

// DynamicHooks
#include "conventions/x86MsCdecl.h"
#include "conventions/x86MsThiscall.h"
#include "conventions/x86MsStdcall.h"
#include "conventions/x86GccCdecl.h"
#include "conventions/x86GccThiscall.h"


DCCallVM* g_pCallVM = dcNewCallVM(4096);
extern std::map<CHook *, std::map<HookType_t, std::list<PyObject *> > > g_mapCallbacks;

dict g_oExposedClasses;


//-----------------------------------------------------------------------------
// Functions
//-----------------------------------------------------------------------------
int GetDynCallConvention(Convention_t eConv)
{
	switch (eConv)
	{
		case CONV_CDECL: return DC_CALL_C_DEFAULT;
		case CONV_THISCALL:
			#ifdef _WIN32
				return DC_CALL_C_X86_WIN32_THIS_MS;
			#else
				return DC_CALL_C_X86_WIN32_THIS_GNU;
			#endif
#ifdef _WIN32
		case CONV_STDCALL: return DC_CALL_C_X86_WIN32_STD;
#endif
	}

	BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Unsupported calling convention.")
	return -1;
}

ICallingConvention* MakeDynamicHooksConvention(Convention_t eConv, std::vector<DataType_t> vecArgTypes, DataType_t returnType, int iAlignment=4)
{
#ifdef _WIN32
	switch (eConv)
	{
	case CONV_CDECL: return new x86MsCdecl(vecArgTypes, returnType, iAlignment);
	case CONV_THISCALL: return new x86MsThiscall(vecArgTypes, returnType, iAlignment);
	case CONV_STDCALL: return new x86MsStdcall(vecArgTypes, returnType, iAlignment);
	}
#else
	switch (eConv)
	{
	case CONV_CDECL: return new x86GccCdecl(vecArgTypes, returnType, iAlignment);
	case CONV_THISCALL: return new x86GccThiscall(vecArgTypes, returnType, iAlignment);
	}
#endif

	BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Unsupported calling convention.")
	return NULL;
}

//-----------------------------------------------------------------------------
// CPointer class
//-----------------------------------------------------------------------------
CPointer::CPointer(unsigned long ulAddr /* = 0 */, bool bAutoDealloc /* false */)
{
	m_ulAddr = ulAddr;
	m_bAutoDealloc = bAutoDealloc;
}

const char * CPointer::GetStringArray(int iOffset /* = 0 */)
{
	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Pointer is NULL.")

	return (const char *) m_ulAddr + iOffset;
}

void CPointer::SetStringArray(char* szText, int iOffset /* = 0 */)
{
	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Pointer is NULL.")

	strcpy((char *) (m_ulAddr + iOffset), szText);
}

CPointer* CPointer::GetPtr(int iOffset /* = 0 */)
{
	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Pointer is NULL.")

	return new CPointer(*(unsigned long *) (m_ulAddr + iOffset));
}

void CPointer::SetPtr(object oPtr, int iOffset /* = 0 */)
{
	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Pointer is NULL.")

	CPointer* pPtr = ExtractPointer(oPtr);
	*(unsigned long *) (m_ulAddr + iOffset) = pPtr->m_ulAddr;
}

int CPointer::Compare(object oOther, unsigned long ulNum)
{
	CPointer* pOther = ExtractPointer(oOther);
	if (!m_ulAddr || !pOther->IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "At least one pointer is NULL.")

	return memcmp((void *) m_ulAddr, (void *) pOther->m_ulAddr, ulNum);
}

bool CPointer::IsOverlapping(object oOther, unsigned long ulNumBytes)
{
	CPointer* pOther = ExtractPointer(oOther);
	if (m_ulAddr <= pOther->m_ulAddr)
		return m_ulAddr + ulNumBytes > pOther->m_ulAddr;
       
	return pOther->m_ulAddr + ulNumBytes > m_ulAddr;
}

CPointer* CPointer::SearchBytes(object oBytes, unsigned long ulNumBytes)
{
	if (!m_ulAddr)
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Pointer is NULL.")

	unsigned long iByteLen = len(oBytes);
	if (ulNumBytes < iByteLen)
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Search range is too small.")

	unsigned char* base  = (unsigned char *) m_ulAddr;
	unsigned char* end   = (unsigned char *) (m_ulAddr + ulNumBytes - (iByteLen - 1));

	unsigned char* bytes = NULL;
	PyArg_Parse(oBytes.ptr(), "y", &bytes);
	if(!bytes)
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Unable to parse the signature.");

	while (base < end)
	{
		unsigned long i = 0;
		for(; i < iByteLen; i++)
		{
			if (bytes[i] == '\x2A')
				continue;

			if (bytes[i] != base[i])
				break;
		}

		if (i == iByteLen)
			return new CPointer((unsigned long) base);

		base++;
	}
	return new CPointer();
}

void CPointer::Copy(object oDest, unsigned long ulNumBytes)
{
	CPointer* pDest = ExtractPointer(oDest);
	if (!m_ulAddr || !pDest->IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "At least one pointer is NULL.")

	if (IsOverlapping(oDest, ulNumBytes))
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Pointers are overlapping!")

	memcpy((void *) pDest->m_ulAddr, (void *) m_ulAddr, ulNumBytes);
}

void CPointer::Move(object oDest, unsigned long ulNumBytes)
{
	CPointer* pDest = ExtractPointer(oDest);
	if (!m_ulAddr || !pDest->IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "At least one pointer is NULL.")

	memmove((void *) pDest->m_ulAddr, (void *) m_ulAddr, ulNumBytes);
}

CPointer* CPointer::GetVirtualFunc(int iIndex)
{
	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Pointer is NULL.")

	void** vtable = *(void ***) m_ulAddr;
	if (!vtable)
		return new CPointer();

	return new CPointer((unsigned long) vtable[iIndex]);
}

CPointer* CPointer::Realloc(int iSize)
{ 
	return new CPointer((unsigned long) UTIL_Realloc((void *) m_ulAddr, iSize)); 
}

CFunction* CPointer::MakeFunction(object oCallingConvention, object args, object oReturnType)
{
	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Pointer is NULL.")

	Convention_t _eCallingConvention = CONV_NONE;
	int _iCallingConvention = -1;
	object _oCallingConvention = object();
	ICallingConvention* _pCallingConvention = NULL;
	tuple _tArgs = tuple(args);
	object _oReturnType = oReturnType;
	DataType_t _eReturnType;

	// Get the enum return type
	try
	{
		_eReturnType = extract<DataType_t>(oReturnType);
	}
	catch( ... )
	{
		PyErr_Clear();
		_eReturnType = DATA_TYPE_POINTER;
	}

	try
	{
		// If this line succeeds the user wants to create a function with the built-in calling conventions
		_eCallingConvention = extract<Convention_t>(oCallingConvention);
		_iCallingConvention = GetDynCallConvention(_eCallingConvention);
		_pCallingConvention = MakeDynamicHooksConvention(_eCallingConvention, ObjectToDataTypeVector(args), _eReturnType);
	}
	catch( ... )
	{
		// If this happens, we won't be able to call the function, because we are using a custom calling convention
		PyErr_Clear();
	
		_oCallingConvention = oCallingConvention(args, _eReturnType);
		_pCallingConvention = extract<ICallingConvention*>(_oCallingConvention);
	}
	
	return new CFunction(m_ulAddr, _eCallingConvention, _iCallingConvention, _oCallingConvention, _pCallingConvention, _tArgs, _eReturnType, _oReturnType);
}

CFunction* CPointer::MakeVirtualFunction(int iIndex, object oCallingConvention, object args, object return_type)
{
	return GetVirtualFunc(iIndex)->MakeFunction(oCallingConvention, args, return_type);
}

void CPointer::CallCallback(PyObject* self, char* szCallback)
{
	if (PyObject_HasAttrString(self, szCallback))
	{
		PyObject* callback = PyObject_GetAttrString(self, szCallback);
		if (callback && PyCallable_Check(callback))
		{
			if (!PyObject_HasAttrString(callback, "__self__"))
			{
				xdecref(PyObject_CallFunction(callback, "O", self));
			}
			else
			{
				xdecref(PyObject_CallMethod(self, szCallback, NULL));
			}
		}
		xdecref(callback);
	}
}

void CPointer::PreDealloc(PyObject* self)
{
	CallCallback(self, "on_dealloc");
	CPointer* pointer = extract<CPointer *>(self);
	pointer->Dealloc();
}

CPointer* CPointer::PreRealloc(PyObject* self, int iSize)
{
	CallCallback(self, "on_realloc");
	CPointer* pointer = extract<CPointer *>(self);
	return pointer->Realloc(iSize);
}
 
void CPointer::__del__(PyObject* self)
{
	CPointer* ptr = extract<CPointer *>(self);
	if (ptr->m_bAutoDealloc)
	{
		PythonLog(4, "Automatically deallocating pointer at %u.", ptr->m_ulAddr);
		PreDealloc(self);
	}
}

//-----------------------------------------------------------------------------
// CFunction class
//-----------------------------------------------------------------------------
CFunction::CFunction(unsigned long ulAddr, Convention_t eCallingConvention,
		int iCallingConvention, object oCallingConvention,
		ICallingConvention* pCallingConvention, tuple tArgs,
		DataType_t eReturnType, object oReturnType)
	:CPointer(ulAddr)
{
	m_eCallingConvention = eCallingConvention;
	m_iCallingConvention = iCallingConvention;
	m_oCallingConvention = oCallingConvention;
	m_pCallingConvention = pCallingConvention;
	m_tArgs = tArgs;
	m_eReturnType = eReturnType;
	m_oReturnType = oReturnType;
}

bool CFunction::IsCallable()
{
	return (m_eCallingConvention != CONV_NONE) && (m_iCallingConvention != -1);
}

bool CFunction::IsHookable()
{
	return m_pCallingConvention != NULL;
}

object CFunction::Call(tuple args, dict kw)
{
	if (!IsCallable())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Function is not callable.")

	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Function pointer is NULL.")

	if (len(args) != len(m_tArgs))
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Number of passed arguments is not equal to the required number.")

	// Reset VM and set the calling convention
	dcReset(g_pCallVM);
	dcMode(g_pCallVM, m_iCallingConvention);

	// Loop through all passed arguments and add them to the VM
	for(int i=0; i < len(args); i++)
	{
		object arg = args[i];
		switch(extract<DataType_t>(m_tArgs[i]))
		{
			case DATA_TYPE_BOOL:      dcArgBool(g_pCallVM, extract<bool>(arg)); break;
			case DATA_TYPE_CHAR:      dcArgChar(g_pCallVM, extract<char>(arg)); break;
			case DATA_TYPE_UCHAR:     dcArgChar(g_pCallVM, extract<unsigned char>(arg)); break;
			case DATA_TYPE_SHORT:     dcArgShort(g_pCallVM, extract<short>(arg)); break;
			case DATA_TYPE_USHORT:    dcArgShort(g_pCallVM, extract<unsigned short>(arg)); break;
			case DATA_TYPE_INT:       dcArgInt(g_pCallVM, extract<int>(arg)); break;
			case DATA_TYPE_UINT:      dcArgInt(g_pCallVM, extract<unsigned int>(arg)); break;
			case DATA_TYPE_LONG:      dcArgLong(g_pCallVM, extract<long>(arg)); break;
			case DATA_TYPE_ULONG:     dcArgLong(g_pCallVM, extract<unsigned long>(arg)); break;
			case DATA_TYPE_LONG_LONG:  dcArgLongLong(g_pCallVM, extract<long long>(arg)); break;
			case DATA_TYPE_ULONG_LONG: dcArgLongLong(g_pCallVM, extract<unsigned long long>(arg)); break;
			case DATA_TYPE_FLOAT:     dcArgFloat(g_pCallVM, extract<float>(arg)); break;
			case DATA_TYPE_DOUBLE:    dcArgDouble(g_pCallVM, extract<double>(arg)); break;
			case DATA_TYPE_POINTER:
			{
				unsigned long ulAddr = 0;
				if (arg.ptr() != Py_None)
					ulAddr = ExtractPointer(arg)->m_ulAddr;
				dcArgPointer(g_pCallVM, ulAddr);
			} break;
			case DATA_TYPE_STRING:    dcArgPointer(g_pCallVM, (unsigned long) (void *) extract<char *>(arg)); break;
			default: BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Unknown argument type.")
		}
	}

	// Call the function
	switch(m_eReturnType)
	{
		case DATA_TYPE_VOID:      dcCallVoid(g_pCallVM, m_ulAddr); break;
		case DATA_TYPE_BOOL:      return object(dcCallBool(g_pCallVM, m_ulAddr));
		case DATA_TYPE_CHAR:      return object(dcCallChar(g_pCallVM, m_ulAddr));
		case DATA_TYPE_UCHAR:     return object((unsigned char) dcCallChar(g_pCallVM, m_ulAddr));
		case DATA_TYPE_SHORT:     return object(dcCallShort(g_pCallVM, m_ulAddr));
		case DATA_TYPE_USHORT:    return object((unsigned short) dcCallShort(g_pCallVM, m_ulAddr));
		case DATA_TYPE_INT:       return object(dcCallInt(g_pCallVM, m_ulAddr));
		case DATA_TYPE_UINT:      return object((unsigned int) dcCallInt(g_pCallVM, m_ulAddr));
		case DATA_TYPE_LONG:      return object(dcCallLong(g_pCallVM, m_ulAddr));
		case DATA_TYPE_ULONG:     return object((unsigned long) dcCallLong(g_pCallVM, m_ulAddr));
		case DATA_TYPE_LONG_LONG:  return object(dcCallLongLong(g_pCallVM, m_ulAddr));
		case DATA_TYPE_ULONG_LONG: return object((unsigned long long) dcCallLongLong(g_pCallVM, m_ulAddr));
		case DATA_TYPE_FLOAT:     return object(dcCallFloat(g_pCallVM, m_ulAddr));
		case DATA_TYPE_DOUBLE:    return object(dcCallDouble(g_pCallVM, m_ulAddr));
		case DATA_TYPE_POINTER:
		{
			CPointer* pPtr = new CPointer(dcCallPointer(g_pCallVM, m_ulAddr));
			if (!m_oReturnType.is_none())
				return m_oReturnType(ptr(pPtr));
			return object(ptr(pPtr));
		}
		case DATA_TYPE_STRING:    return object((const char *) dcCallPointer(g_pCallVM, m_ulAddr));
		default: BOOST_RAISE_EXCEPTION(PyExc_TypeError, "Unknown return type.")
	}
	return object();
}

object CFunction::CallTrampoline(tuple args, dict kw)
{
	if (!IsCallable())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Function is not callable.")

	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Function pointer is NULL.")

	CHook* pHook = GetHookManager()->FindHook((void *) m_ulAddr);
	if (!pHook)
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Function was not hooked.")

	return CFunction((unsigned long) pHook->m_pTrampoline, m_eCallingConvention,
		m_iCallingConvention, m_oCallingConvention, m_pCallingConvention, m_tArgs,
		m_eReturnType, m_oReturnType).Call(args, kw);
}

handle<> CFunction::AddHook(HookType_t eType, PyObject* pCallable)
{
	if (!IsHookable())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Function is not hookable.")

	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Function pointer is NULL.")

	// Hook the function
	CHook* pHook = GetHookManager()->HookFunction((void *) m_ulAddr, m_pCallingConvention);
	
	// Add the hook handler. If it's already added, it won't be added twice
	pHook->AddCallback(eType, (HookHandlerFn *) (void *) &SP_HookHandler);
	
	// Add the callback to our map
	g_mapCallbacks[pHook][eType].push_back(pCallable);
	
	// Return the callback, so we can use this method as a decorator
	return handle<>(borrowed(pCallable));
}

void CFunction::RemoveHook(HookType_t eType, PyObject* pCallable)
{
	if (!IsValid())
		BOOST_RAISE_EXCEPTION(PyExc_ValueError, "Function pointer is NULL.")
		
	CHook* pHook = GetHookManager()->FindHook((void *) m_ulAddr);
	if (!pHook)
		return;

	g_mapCallbacks[pHook][eType].remove(pCallable);
}