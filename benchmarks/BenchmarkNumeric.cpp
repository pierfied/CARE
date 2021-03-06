//////////////////////////////////////////////////////////////////////////////////////
// Copyright 2020 Lawrence Livermore National Security, LLC and other CARE developers.
// See the top-level LICENSE file for details.
//
// SPDX-License-Identifier: BSD-3-Clause
//////////////////////////////////////////////////////////////////////////////////////

// CARE headers
#include "care/care.h"
#include "care/numeric.h"

// Other library headers
#include <benchmark/benchmark.h>

// Std library headers
#include <climits>
#include <numeric>

static void benchmark_std_iota(benchmark::State& state) {
   // Perform setup here
   const int size = state.range(0);

   care::host_device_ptr<int> data(size, "data");

   // TODO: Can probably just use data.data() in the future,
   // but we are currently using an older version of CHAI
   // in some projects that depend on CARE.
#if defined(CARE_ENABLE_IMPLICIT_CONVERSIONS)
   int* host_data = data;
#else
   int* host_data = data.data();
#endif

   while (state.KeepRunning()) {
      std::iota(host_data, host_data + size, 0);
   }
}

static void benchmark_care_iota(benchmark::State& state) {
   // Perform setup here
   const int size = state.range(0);

   care::host_device_ptr<int> data(size, "data");

   while (state.KeepRunning()) {
      care::iota(RAJA::seq_exec{}, data, size, 0);
   }
}

// Register the function as a benchmark
BENCHMARK(benchmark_std_iota)->Range(1, INT_MAX);
BENCHMARK(benchmark_care_iota)->Range(1, INT_MAX);

// Run the benchmark
BENCHMARK_MAIN();

