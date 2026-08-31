/*
 * The WebAssembly Live Migration Project
 *
 * SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 * Copyright 2025 Regents of the University of California
 * UC Santa Cruz Sluglab.
 */

#ifndef MVVM_EXPORT_H
#define MVVM_EXPORT_H

#ifdef _WIN32
#ifdef MVVM_EXPORT_EXPORTS
#define MVVM_API __declspec(dllexport)
#else
#define MVVM_API __declspec(dllimport)
#endif
#else
#define MVVM_API
#endif

#endif // MVVM_EXPORT_H