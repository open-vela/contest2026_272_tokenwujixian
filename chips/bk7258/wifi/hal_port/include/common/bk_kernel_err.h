/*
 * chips/bk7258/wifi/hal_port/include/common/bk_kernel_err.h
 *
 * NuttX reimplementation of the Armino kernel error codes used by the
 * vendored glue. Values are interface-compatible.
 */

#ifndef __BK7258_WIFI_GLUE_COMMON_BK_KERNEL_ERR_H
#define __BK7258_WIFI_GLUE_COMMON_BK_KERNEL_ERR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <common/bk_err.h>

#define kNoErr                      0
#define kGeneralErr                -1
#define kInProgressErr              1
#define kGenericErrorBase           -6700
#define kUnknownErr                 -6700
#define kOptionErr                  -6701
#define kSelectorErr                -6702
#define kExecutionStateErr          -6703
#define kPathErr                    -6704
#define kParamErr                   -6705
#define kUserRequiredErr            -6706
#define kCommandErr                 -6707
#define kIDErr                      -6708
#define kStateErr                   -6709
#define kRangeErr                   -6710
#define kRequestErr                 -6711
#define kResponseErr                -6712
#define kChecksumErr                -6713
#define kNotHandledErr              -6714
#define kVersionErr                 -6715
#define kSignatureErr               -6716
#define kFormatErr                  -6717
#define kNotInitializedErr          -6718
#define kAlreadyInitializedErr      -6719
#define kNotInUseErr                -6720
#define kAlreadyInUseErr            -6721
#define kTimeoutErr                 -6722
#define kCanceledErr                -6723
#define kAlreadyCanceledErr         -6724
#define kCannotCancelErr            -6725
#define kDeletedErr                 -6726
#define kNotFoundErr                -6727
#define kNoMemoryErr                -6728
#define kNoResourcesErr             -6729
#define kDuplicateErr               -6730
#define kImmutableErr               -6731
#define kUnsupportedDataErr         -6732
#define kIntegrityErr               -6733
#define kIncompatibleErr            -6734
#define kUnsupportedErr             -6735
#define kUnexpectedErr              -6736
#define kValueErr                   -6737
#define kNotReadableErr             -6738
#define kNotWritableErr             -6739
#define kBadReferenceErr            -6740
#define kFlagErr                    -6741
#define kMalformedErr               -6742
#define kSizeErr                    -6743
#define kNameErr                    -6744
#define kNotPreparedErr             -6745
#define kReadErr                    -6746
#define kWriteErr                   -6747
#define kMismatchErr                -6748
#define kDateErr                    -6749
#define kUnderrunErr                -6750
#define kOverrunErr                 -6751
#define kEndingErr                  -6752
#define kConnectionErr              -6753
#define kAuthenticationErr          -6754
#define kOpenErr                    -6755
#define kTypeErr                    -6756
#define kSkipErr                    -6757
#define kNoAckErr                   -6758
#define kCollisionErr               -6759
#define kBackoffErr                 -6760

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_COMMON_BK_KERNEL_ERR_H */
