/*
 * Copyright (C) 2004, 2005, 2006, 2007, 2013 Apple Inc. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#pragma once

/* This prefix file should contain only:
 *    1) files to precompile for faster builds
 *    2) in one case at least: OS-X-specific performance bug workarounds
 *    3) the special trick to catch us using new or delete without including "config.h"
 * The project should be able to build without this header, although we rarely test that.
 */

/* Things that need to be defined globally should go into "config.h". */

#if defined(HAVE_CONFIG_H) && HAVE_CONFIG_H && defined(BUILDING_WITH_CMAKE)
#include "cmakeconfig.h"
#endif

#include <wtf/Platform.h>

#if defined(__APPLE__)
#ifdef __cplusplus
#include <cstddef>
#else
#include <stddef.h>
#endif
#endif

#if !OS(WINDOWS)
#include <pthread.h>
#endif // !OS(WINDOWS)

#include <sys/types.h>
#include <fcntl.h>
#if HAVE(REGEX_H)
#include <regex.h>
#endif

#include <setjmp.h>

#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__APPLE__)
#include <unistd.h>
#endif

#ifdef __cplusplus
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <typeinfo>
#include <wtf/Variant.h>
#endif

#if defined(__APPLE__)
#include <sys/param.h>
#endif
#include <sys/stat.h>
#if defined(__APPLE__)
#include <sys/time.h>
#include <sys/resource.h>
#endif

#if USE(CF)
#include <CoreFoundation/CoreFoundation.h>
#endif

#if USE(CG)
#include <CoreGraphics/CoreGraphics.h>
#endif

#if OS(WINDOWS)
#ifndef CF_IMPLICIT_BRIDGING_ENABLED
#define CF_IMPLICIT_BRIDGING_ENABLED
#endif

#ifndef CF_IMPLICIT_BRIDGING_DISABLED
#define CF_IMPLICIT_BRIDGING_DISABLED
#endif

#if USE(CF)
#include <CoreFoundation/CFBase.h>
#endif

#ifndef CF_ENUM
#define CF_ENUM(_type, _name) _type _name; enum
#endif
#ifndef CF_OPTIONS
#define CF_OPTIONS(_type, _name) _type _name; enum
#endif
#ifndef CF_ENUM_DEPRECATED
#define CF_ENUM_DEPRECATED(_macIntro, _macDep, _iosIntro, _iosDep)
#endif
#ifndef CF_ENUM_AVAILABLE
#define CF_ENUM_AVAILABLE(_mac, _ios)
#endif
#endif

#if PLATFORM(WIN)
#include <windows.h>
#else

#if OS(WINDOWS)
#include <windows.h>
#endif // OS(WINDOWS)

#if USE(OS_LOG)
#include <os/log.h>
#endif

#if PLATFORM(IOS_FAMILY)
#include <MobileCoreServices/MobileCoreServices.h>
#endif

#if PLATFORM(MAC)
#if !USE(APPLE_INTERNAL_SDK)
/* SecTrustedApplication.h declares SecTrustedApplicationCreateFromPath(...) to
 * be unavailable on macOS, so do not include that header. */
#define _SECURITY_SECTRUSTEDAPPLICATION_H_
#endif
#include <CoreServices/CoreServices.h>
#endif

#endif

#ifdef __OBJC__
#if PLATFORM(IOS_FAMILY)
#import <Foundation/Foundation.h>
#else
#if USE(APPKIT)
#import <Cocoa/Cocoa.h>
#endif
#endif // PLATFORM(IOS_FAMILY)
#endif

#ifdef __cplusplus

#if !PLATFORM(WIN)

#include <JavaScriptCore/ArrayBuffer.h>
#include <JavaScriptCore/CPU.h>
#include <JavaScriptCore/Forward.h>
#include <JavaScriptCore/JSCConfig.h>
#include <JavaScriptCore/OptionsList.h>
#include <JavaScriptCore/SourceID.h>
#include <JavaScriptCore/Weak.h>
#include <JavaScriptCore/WeakInlinesLight.h>
#include <set>
#include <unicode/uscript.h>
#include <wtf/AbstractRefCountedAndCanMakeWeakPtr.h>
#include <wtf/ArgumentCoder.h>
#include <wtf/Box.h>
#include <wtf/CheckedPtr.h>
#include <wtf/CompletionHandler.h>
#include <wtf/CrossThreadCopier.h>
#include <wtf/CryptographicallyRandomNumber.h>
#include <wtf/DataLog.h>
#include <wtf/DoublyLinkedList.h>
#include <wtf/FastMalloc.h>
#include <wtf/FileSystem.h>
#include <wtf/FixedVector.h>
#include <wtf/HashCountedSet.h>
#include <wtf/HashMap.h>
#include <wtf/HashSet.h>
#include <wtf/ListHashSet.h>
#include <wtf/Logger.h>
#include <wtf/MappedFileData.h>
#include <wtf/NumberOfCores.h>
#include <wtf/RefCountedAndCanMakeWeakPtr.h>
#include <wtf/RobinHoodHashMap.h>
#include <wtf/RobinHoodHashSet.h>
#include <wtf/StdLibExtras.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/ThreadSafeWeakHashSet.h>
#include <wtf/TriState.h>
#include <wtf/URLHash.h>
#include <wtf/WeakHashMap.h>
#include <wtf/WorkQueue.h>
#include <wtf/text/AtomString.h>
#include <wtf/text/AtomStringHash.h>
#include <wtf/text/ParsingUtilities.h>
#include <wtf/text/StringHash.h>
#include <wtf/text/WTFString.h>

#if USE(CF)
#include <wtf/cf/TypeCastsCF.h>
#endif

#if PLATFORM(COCOA)
#include <objc/runtime.h>
#include <wtf/OSObjectPtr.h>
#endif

#include "Color.h"
#include "ContextDestructionObserver.h"
#include "EventTarget.h"
#include "ExceptionOr.h"
#include "FloatConversion.h"
#include "FloatRect.h"
#include "FrameIdentifier.h"
#include "GraphicsTypes.h"
#include "HitTestRequest.h"
#include "ImageTypes.h"
#include "IntRect.h"
#include "LayoutRect.h"
#include "LayoutSize.h"
#include "NodeIdentifier.h"
#include "NodeType.h"
#include "PageIdentifier.h"
#include "ProcessQualified.h"
#include "PublicSuffixStore.h"
#include "QualifiedName.h"
#include "RenderPtr.h"
#include "RenderStyleConstants.h"
#include "RenderingResource.h"
#include "ScriptExecutionContext.h"
#include "ScriptExecutionContextIdentifier.h"
#include "ScriptWrappable.h"
#include "ScrollTypes.h"
#include "SecurityContext.h"
#include "SecurityOriginData.h"
#include "ServiceWorkerIdentifier.h"
#include "SharedBuffer.h"
#include "Timer.h"

#include <wtf/DateMath.h>
#endif

#define new ("if you use new/delete make sure to include config.h at the top of the file"()) 
#define delete ("if you use new/delete make sure to include config.h at the top of the file"()) 
#endif

/* When C++ exceptions are disabled, the C++ library defines |try| and |catch|
 * to allow C++ code that expects exceptions to build. These definitions
 * interfere with Objective-C++ uses of Objective-C exception handlers, which
 * use |@try| and |@catch|. As a workaround, undefine these macros. */
#ifdef __OBJC__
#undef try
#undef catch
#endif

