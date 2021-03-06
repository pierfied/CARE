//////////////////////////////////////////////////////////////////////////////////////
// Copyright 2020 Lawrence Livermore National Security, LLC and other CARE developers.
// See the top-level LICENSE file for details.
//
// SPDX-License-Identifier: BSD-3-Clause
//////////////////////////////////////////////////////////////////////////////////////

#include "care/config.h"

#if CARE_HAVE_LOOP_FUSER

#define GPU_ACTIVE
#define HAVE_FUSER_TEST CARE_HAVE_LOOP_FUSER
#if HAVE_FUSER_TEST
// always have DEBUG on to force the packer to be on for CPU builds.
#ifdef DEBUG
#undef DEBUG
#define DEBUG 1
#endif
#include "gtest/gtest.h"

#include "care/LoopFuser.h"
#include "care/care.h"
#include "care/util.h"

// This makes it so we can use device lambdas from within a GPU_TEST
#define GPU_TEST(X, Y) static void gpu_test_ ## X_ ## Y(); \
   TEST(X, Y) { gpu_test_ ## X_ ## Y(); } \
   static void gpu_test_ ## X_ ## Y()

TEST(UpperBound_binarySearch, checkOffsets) {
   // These come from a segfault that occurred during development
   int offsetArr[] = { 17, 34, 51, 68, 85, 102, 119, 136, 153, 170, 187, 204, 221, 238, 255, 272, 289, 306, 323, 340 };
   int offset;

   offset = care::binarySearch<int>(offsetArr, 0, 1, 0, true);
   EXPECT_EQ(offsetArr[offset], 17);

   offset = care::binarySearch<int>(offsetArr, 0, 20, 37, true);
   EXPECT_EQ(offsetArr[offset], 51);

   offset = care::binarySearch<int>(offsetArr, 0, 20, 33, true);
   EXPECT_EQ(offsetArr[offset], 34);

   offset = care::binarySearch<int>(offsetArr, 0, 20, 34, true);
   EXPECT_EQ(offsetArr[offset], 51);

   offset = care::binarySearch<int>(offsetArr, 0, 20, 339, true);
   EXPECT_EQ(offsetArr[offset], 340);

   offset = care::binarySearch<int>(offsetArr, 0, 20, 340, true);
   EXPECT_EQ(offset, -1);
}

GPU_TEST(TestPacker, packFixedRange) {
   LoopFuser * packer = LoopFuser::getInstance();
   packer->start();

   int arrSize = 1024;
   care::host_device_ptr<int> src(arrSize);
   care::host_device_ptr<int> dst(arrSize);

   // initialize the src and dst on the host
   LOOP_SEQUENTIAL(i, 0, arrSize) {
      src[i] = i;
      dst[i] = -1;
   } LOOP_SEQUENTIAL_END

   int pos = 0;
   packer->registerAction(0, arrSize, pos,
                          [=] CARE_DEVICE(int, bool, int, int, int) {
      return true;
   },
                          [=] CARE_DEVICE(int i, bool, int, int, int) {
      dst[pos+i] = src[i];
      return 0;
   });

   // pack has not been flushed, so
   // host data should not be updated yet
   int * host_dst = dst.getPointer(care::CPU, false);
   int * host_src = src.getPointer(care::CPU, false);

   for (int i = 0; i < 2; ++i) {
      EXPECT_EQ(host_dst[i], -1);
      EXPECT_EQ(host_src[i], i);
   }

   packer->flush();

   care::gpuDeviceSynchronize();

#ifdef __GPUCC__
   // pack should have happened on the device, so
   // host data should not be updated yet
   for (int i = 0; i < 2; ++i) {
      EXPECT_EQ(host_dst[i], -1);
      EXPECT_EQ(host_src[i], i);
   }
#endif

   // bringing stuff back to the host, dst[i] should now be i
   LOOP_SEQUENTIAL(i, 0, arrSize) {
      EXPECT_EQ(dst[i], i);
      EXPECT_EQ(src[i], i);
   } LOOP_SEQUENTIAL_END

   src.free();
   dst.free();
}

GPU_TEST(TestPacker, packFixedRangeMacro) {
   FUSIBLE_LOOPS_START

   int arrSize = 1024;
   care::host_device_ptr<int> src(arrSize);
   care::host_device_ptr<int> dst(arrSize);

   // initialize the src and dst on the host
   LOOP_SEQUENTIAL(i, 0, arrSize) {
      src[i] = i;
      dst[i] = -1;
   } LOOP_SEQUENTIAL_END

   int pos = 0;

   {
      auto __fuser__ = LoopFuser::getInstance();
      auto __fusible_offset__ = __fuser__->getOffset();
      int scan_pos = 0;
      __fuser__->registerAction(0, arrSize, scan_pos,
                                [=] FUSIBLE_DEVICE(int, bool, int, int, int)->bool { return true; },
                                [=] FUSIBLE_DEVICE(int i, bool, int, int, int) {
         i += 0 -  __fusible_offset__ ;
         if (i < arrSize) {
            dst[pos+i] = src[i];
         }
         return 0;
      });
   }

   care::gpuDeviceSynchronize();

   // pack should  not have happened yet so
   // host data should not be updated yet
   int * host_dst = dst.getPointer(care::CPU, false);
   int * host_src = src.getPointer(care::CPU, false);

   for (int i = 0; i < arrSize; ++i) {
      EXPECT_EQ(host_dst[i], -1);
      EXPECT_EQ(host_src[i], i);
   }

   FUSIBLE_LOOPS_STOP

   // bringing stuff back to the host, dst[i] should now be i
   LOOP_SEQUENTIAL(i, 0, arrSize) {
      EXPECT_EQ(dst[i], i);
      EXPECT_EQ(src[i], i);
   } LOOP_SEQUENTIAL_END

   src.free();
   dst.free();
}

GPU_TEST(TestPacker, fuseFixedRangeMacro) {
   FUSIBLE_LOOPS_START
   int arrSize = 8;
   care::host_device_ptr<int> src(arrSize);
   care::host_device_ptr<int> dst(arrSize);
   // initialize the src and dst on the host
   LOOP_SEQUENTIAL(i, 0, arrSize) {
      src[i] = i;
      dst[i] = -1;
   } LOOP_SEQUENTIAL_END
   int pos = 0;
   FUSIBLE_LOOP_STREAM(i, 0, arrSize/2) {
      dst[pos+i] = src[i];
   } FUSIBLE_LOOP_STREAM_END
   pos += arrSize/2;
   FUSIBLE_LOOP_STREAM(i, 0, arrSize/2) {
      dst[pos+i] = src[i+pos]*2;
   } FUSIBLE_LOOP_STREAM_END

   care::gpuDeviceSynchronize();

   // pack should  not have happened yet so
   // host data should not be updated yet
   int * host_dst = dst.getPointer(care::CPU, false);
   int * host_src = src.getPointer(care::CPU, false);
   for (int i = 0; i < arrSize; ++i) {
      EXPECT_EQ(host_dst[i], -1);
      EXPECT_EQ(host_src[i], i);
   }
   FUSIBLE_LOOPS_STOP
   // bringing stuff back to the host, dst[i] should now be i
   LOOP_SEQUENTIAL(i, 0, arrSize/2) {
      EXPECT_EQ(dst[i], i);
      EXPECT_EQ(src[i], i);
   } LOOP_SEQUENTIAL_END
   LOOP_SEQUENTIAL(i, arrSize/2, arrSize) {
      EXPECT_EQ(dst[i], i*2);
      EXPECT_EQ(src[i], i);
   } LOOP_SEQUENTIAL_END
}

GPU_TEST(performanceWithoutPacker, allOfTheStreams) {
   int arrSize = 128;
   int timesteps = 5;
   care::host_device_ptr<int> src(arrSize);
   care::host_device_ptr<int> dst(arrSize);
   // initialize the src and dst on the device
   for (int t = 0; t < timesteps; ++t) {
      LOOP_STREAM(i, 0, arrSize) {
         src[i] = i;
         dst[i] = -1;
      } LOOP_STREAM_END
      for (int i = 0; i < arrSize; ++i) {
         LOOP_STREAM(j, i, i+1) {
            dst[j] = src[j];
         } LOOP_STREAM_END
      }
      // bringing stuff back to the host, dst[i] should now be i
      LOOP_SEQUENTIAL(i, 0, arrSize) {
         EXPECT_EQ(dst[i], i);
         EXPECT_EQ(src[i], i);
      } LOOP_SEQUENTIAL_END
   }
}


GPU_TEST(performanceWithPacker, allOfTheFuses) {
   int arrSize = 128;
   int timesteps = 5;
   care::host_device_ptr<int> src(arrSize);
   care::host_device_ptr<int> dst(arrSize);
   for (int t = 0; t < timesteps; ++t) {
      // initialize the src and dst on the device
      LOOP_STREAM(i, 0, arrSize) {
         src[i] = i;
         dst[i] = -1;
      } LOOP_STREAM_END
      FUSIBLE_LOOPS_START
      for (int i = 0; i < arrSize; ++i) {
         FUSIBLE_LOOP_STREAM(j, i, i+1) {
            dst[j] = src[j];
         } FUSIBLE_LOOP_STREAM_END
      }
      FUSIBLE_LOOPS_STOP
      // bringing stuff back to the host, dst[i] should now be i
      LOOP_SEQUENTIAL(i, 0, arrSize) {
         EXPECT_EQ(dst[i], i);
         EXPECT_EQ(src[i], i);
      } LOOP_SEQUENTIAL_END
   }
}


GPU_TEST(orderDependent, basic_test) {
   int arrSize = 128;
   int timesteps = 5;
   care::host_device_ptr<int> A(arrSize);
   care::host_device_ptr<int> B(arrSize);
   // initialize A on the host
   LOOP_STREAM(i, 0, arrSize) {
      A[i] = 0;
      B[i] = 0;
   } LOOP_STREAM_END
   FUSIBLE_LOOPS_PRESERVE_ORDER_START
   for (int t = 0; t < timesteps; ++t) {
      FUSIBLE_LOOP_STREAM(i, 0, arrSize) {
         if (t %2 == 0) {
            A[i] = (A[i] + 1)<<t;
         }
         else {
            A[i] = (A[i] + 1)>>t;
         }
      } FUSIBLE_LOOP_STREAM_END
      // do the same thing, but as separate kernels in a sequence
      LOOP_STREAM(i, 0, arrSize) {
         if (t %2 == 0) {
            B[i] = (B[i] + 1)<<t;
         }
         else {
            B[i] = (B[i] + 1)>>t;
         }
      } LOOP_STREAM_END
   }
   FUSIBLE_LOOPS_STOP
   // bringing stuff back to the host, A[i] should now be B[i]
   LOOP_SEQUENTIAL(i, 0, arrSize) {
      EXPECT_EQ(A[i], B[i]);
   } LOOP_SEQUENTIAL_END
}
static
FUSIBLE_DEVICE bool printAndAssign(care::host_device_ptr<int> B, int i) {
   return B[i] == 1;
}

GPU_TEST(fusible_scan, basic_fusible_scan) {
   // arrSize should be even for this test
   int arrSize = 4;
   care::host_device_ptr<int> A(arrSize, "A");
   care::host_device_ptr<int> B(arrSize, "B");
   care::host_device_ptr<int> A_scan(arrSize, "A_scan");
   care::host_device_ptr<int> B_scan(arrSize, "B_scan");
   care::host_device_ptr<int> AB_scan(arrSize, "AB_scan");
   // initialize A on the host
   LOOP_STREAM(i, 0, arrSize) {
      A[i] = i%2;
      B[i] = (i+1)%2;
      A_scan[i] = 0;
      B_scan[i] = 0;
      AB_scan[i] = 0;
   } LOOP_STREAM_END

   FUSIBLE_LOOPS_START
   LoopFuser::getInstance()->setVerbose(true);

   int a_pos = 0;
   int b_pos = 0;
   int ab_pos = 0;
   FUSIBLE_LOOP_SCAN(i, 0, arrSize, pos, a_pos, A[i] == 1) {
      A_scan[pos] = 1;
   } FUSIBLE_LOOP_SCAN_END(arrSize, pos, a_pos)
   FUSIBLE_LOOP_SCAN(i, 0, arrSize, pos, b_pos, printAndAssign(B, i)) {
      B_scan[pos] = 1;
   } FUSIBLE_LOOP_SCAN_END(arrSize, pos, b_pos)
   FUSIBLE_LOOP_SCAN(i, 0, arrSize, pos, ab_pos, (A[i] == 1) || (B[i] ==1)) {
      AB_scan[pos] = 1;
   } FUSIBLE_LOOP_SCAN_END(arrSize, pos, ab_pos)

   FUSIBLE_LOOPS_STOP
   LoopFuser::getInstance()->setVerbose(false);

   // sum up the results of the scans
   RAJAReduceSum<int> sumA(0);
   RAJAReduceSum<int> sumB(0);
   RAJAReduceSum<int> sumAB(0);
   LOOP_STREAM(i, 0, arrSize) {
      sumA += A_scan[i];
      sumB += B_scan[i];
      sumAB += AB_scan[i];
   } LOOP_STREAM_END

   // check sums
   EXPECT_EQ((int)sumA, arrSize/2);
   EXPECT_EQ((int)sumB, arrSize/2);
   EXPECT_EQ((int)sumAB, arrSize);
   // check scan positions
   EXPECT_EQ(a_pos, arrSize/2);
   EXPECT_EQ(b_pos, arrSize/2);
   EXPECT_EQ(ab_pos, arrSize);
}

GPU_TEST(fusible_dependent_scan, basic_dependent_fusible_scan) {
   // sarrSize should be even for this test
   int arrSize = 4;
   care::host_device_ptr<int> A(arrSize, "A");
   care::host_device_ptr<int> B(arrSize, "B");
   care::host_device_ptr<int> A_scan(3*arrSize, "A_scan");
   care::host_device_ptr<int> B_scan(3*arrSize, "B_scan");
   care::host_device_ptr<int> AB_scan(3*arrSize, "AB_scan");

   // initialize
   LOOP_STREAM(i, 0, arrSize) {
      A[i] = i%2;
      B[i] = (i+1)%2;
   } LOOP_STREAM_END

   LOOP_STREAM(i, 0, 3*arrSize) {
      A_scan[i] = 0;
      B_scan[i] = 0;
      AB_scan[i] = 0;
   } LOOP_STREAM_END

   FUSIBLE_LOOPS_START

   int result_pos = 0;
   FUSIBLE_LOOP_SCAN(i, 0, arrSize, pos, result_pos, A[i] == 1) {
      A_scan[pos] = 1;
   } FUSIBLE_LOOP_SCAN_END(arrSize, pos, result_pos)
   FUSIBLE_LOOP_SCAN(i, 0, arrSize, pos, result_pos, printAndAssign(B, i)) {
      B_scan[pos] = 1;
   } FUSIBLE_LOOP_SCAN_END(arrSize, pos, result_pos)
   FUSIBLE_LOOP_SCAN(i, 0, arrSize, pos, result_pos, (A[i] == 1) || (B[i] ==1)) {
      AB_scan[pos] = 1;
   } FUSIBLE_LOOP_SCAN_END(arrSize, pos, result_pos)

   FUSIBLE_LOOPS_STOP

   // sum up the results of the scans
   {
      RAJAReduceSum<int> sumA(0);
      RAJAReduceSum<int> sumB(0);
      RAJAReduceSum<int> sumAB(0);
      LOOP_STREAM(i, 0, 3*arrSize) {
         sumA += A_scan[i];
         sumB += B_scan[i];
         sumAB += AB_scan[i];
      } LOOP_STREAM_END

      // check sums
      EXPECT_EQ((int)sumA, arrSize/2);
      EXPECT_EQ((int)sumB, arrSize/2);
      EXPECT_EQ((int)sumAB, arrSize);
   }
   // sum up the results of the scans within the expected ranges of the scans
   {
      RAJAReduceSum<int> sumA(0);
      RAJAReduceSum<int> sumB(0);
      RAJAReduceSum<int> sumAB(0);
      LOOP_STREAM(i, 0, arrSize/2) {
         sumA += A_scan[i];
      } LOOP_STREAM_END
      LOOP_STREAM(i, arrSize/2, arrSize) {
         sumB += B_scan[i];
      } LOOP_STREAM_END
      LOOP_STREAM(i, arrSize, 2*arrSize) {
         sumAB += AB_scan[i];
      } LOOP_STREAM_END

      // check sums
      EXPECT_EQ((int)sumA, arrSize/2);
      EXPECT_EQ((int)sumB, arrSize/2);
      EXPECT_EQ((int)sumAB, arrSize);
   }
   // check scan positions
   EXPECT_EQ(result_pos, arrSize*2);
}

GPU_TEST(fusible_loops_and_scans, mix_and_match) {
   // should be even for this test
   int arrSize = 4;
   care::host_device_ptr<int> A(arrSize, "A");
   care::host_device_ptr<int> B(arrSize, "B");
   care::host_device_ptr<int> C(arrSize, "C");
   care::host_device_ptr<int> D(arrSize, "D");
   care::host_device_ptr<int> E(arrSize, "E");
   care::host_device_ptr<int> A_scan(3*arrSize, "A_scan");
   care::host_device_ptr<int> B_scan(3*arrSize, "B_scan");
   care::host_device_ptr<int> AB_scan(3*arrSize, "AB_scan");
   // initialize A on the host
   LOOP_STREAM(i, 0, arrSize) {
      A[i] = i%2;
      B[i] = (i+1)%2;
   } LOOP_STREAM_END

   LOOP_STREAM(i, 0, 3*arrSize) {
      A_scan[i] = 0;
      B_scan[i] = 0;
      AB_scan[i] = 0;
   } LOOP_STREAM_END

   FUSIBLE_LOOPS_START
   FUSIBLE_LOOP_STREAM(i, 0, arrSize) {
      C[i] = i;
   } FUSIBLE_LOOP_STREAM_END

   int outer_dim = 2;
   care::host_ptr<int> results(outer_dim);
   for (int m = 0; m < outer_dim; ++m) {
      results[m] = 0;
      int & result_pos = results[m];

      FUSIBLE_LOOP_SCAN(i, 0, arrSize, pos, result_pos, A[i] == 1) {
         A_scan[pos] = 1;
      } FUSIBLE_LOOP_SCAN_END(arrSize, pos, result_pos)
      FUSIBLE_LOOP_SCAN(i, 0, arrSize, pos, result_pos, printAndAssign(B, i)) {
         B_scan[pos] = 1;
      } FUSIBLE_LOOP_SCAN_END(arrSize, pos, result_pos)
      FUSIBLE_LOOP_SCAN(i, 0, arrSize, pos, result_pos, (A[i] == 1) || (B[i] ==1)) {
         AB_scan[pos] = 1;
      } FUSIBLE_LOOP_SCAN_END(arrSize, pos, result_pos)
   }
   FUSIBLE_LOOP_STREAM(i, 0, arrSize) {
      D[i] = 2*i;
   } FUSIBLE_LOOP_STREAM_END
   FUSIBLE_LOOP_STREAM(i, 0, arrSize) {
      E[i] = 3*i;
   } FUSIBLE_LOOP_STREAM_END

   FUSIBLE_LOOPS_STOP

   // sum up the results of the scans
   {
      RAJAReduceSum<int> sumA(0);
      RAJAReduceSum<int> sumB(0);
      RAJAReduceSum<int> sumAB(0);
      LOOP_STREAM(i, 0, 3*arrSize) {
         sumA += A_scan[i];
         sumB += B_scan[i];
         sumAB += AB_scan[i];
      } LOOP_STREAM_END

      // check sums
      EXPECT_EQ((int)sumA, arrSize/2);
      EXPECT_EQ((int)sumB, arrSize/2);
      EXPECT_EQ((int)sumAB, arrSize);
   }
   // sum up the results of the scans within the expected ranges of the scans
   {
      RAJAReduceSum<int> sumA(0);
      RAJAReduceSum<int> sumB(0);
      RAJAReduceSum<int> sumAB(0);
      LOOP_STREAM(i, 0, arrSize/2) {
         sumA += A_scan[i];
      } LOOP_STREAM_END
      LOOP_STREAM(i, arrSize/2, arrSize) {
         sumB += B_scan[i];
      } LOOP_STREAM_END
      LOOP_STREAM(i, arrSize, 2*arrSize) {
         sumAB += AB_scan[i];
      } LOOP_STREAM_END

      // check sums
      EXPECT_EQ((int)sumA, arrSize/2);
      EXPECT_EQ((int)sumB, arrSize/2);
      EXPECT_EQ((int)sumAB, arrSize);
   }
   // check scan positions
   for (int m = 0; m < outer_dim; ++m) {
      EXPECT_EQ(results[m], arrSize*2);
   }

   // Check that FUSIBLE STREAM loops interwoven between scans also worked (yes, they had race conditions, but all races should have
   // written the same value)

   LOOP_SEQUENTIAL(i, 0, arrSize) {
      EXPECT_EQ(C[i], i);
      EXPECT_EQ(D[i], 2*i);
      EXPECT_EQ(E[i], 3*i);
   } LOOP_SEQUENTIAL_END
}

GPU_TEST(fusible_scan_custom, basic_fusible_scan_custom) {
   // arrSize should be even for this test
   int arrSize = 4;
   care::host_device_ptr<int> A(arrSize, "A");
   care::host_device_ptr<int> B(arrSize, "B");
   care::host_device_ptr<int> AB_scan(arrSize*2, "AB_scan");
   // initialize A on the host
   LOOP_STREAM(i, 0, arrSize*2) {
      if (i / arrSize == 0) { 
         AB_scan[i] = 2;
      }
      else {
         AB_scan[i] = 3;
      }
   } LOOP_STREAM_END
   // convert to offsets
   exclusive_scan<int, RAJAExec>(AB_scan, nullptr, arrSize*2, RAJA::operators::plus<int>{}, 0, true);
   LOOP_STREAM(i,arrSize,arrSize*2) {
      AB_scan[i] -= AB_scan[arrSize];
   } LOOP_STREAM_END

   FUSIBLE_LOOPS_START
   LoopFuser::getInstance()->setVerbose(true);

   FUSIBLE_LOOP_COUNTS_TO_OFFSETS_SCAN(i, 0, arrSize, A) {
      A[i] = 2;
   } FUSIBLE_LOOP_COUNTS_TO_OFFSETS_SCAN_END(i,arrSize, A)

   FUSIBLE_LOOP_COUNTS_TO_OFFSETS_SCAN(i, 0, arrSize, B) {
      B[i] = 3;
   } FUSIBLE_LOOP_COUNTS_TO_OFFSETS_SCAN_END(i, arrSize, B)

   FUSIBLE_LOOPS_STOP
   LoopFuser::getInstance()->setVerbose(false);

   //  check answer
   LOOP_SEQUENTIAL(i, 0, arrSize*2) {
      int indx = i%arrSize;
      if (i / arrSize == 0 ) { 
         EXPECT_EQ(AB_scan[i],A[indx]);
      }
      else {
         EXPECT_EQ(AB_scan[i],B[indx]);
      }
   } LOOP_SEQUENTIAL_END

}


// TODO: FUSIBLE_LOOP_STREAM Should not batch if FUSIBLE_LOOPS_START has not been called.
// TODO: test with two START and STOP to make sure new stuff is overwriting the old stuff.
//
#endif
#endif // CARE_HAVE_LOOP_FUSER
