// Copyright 2021 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_COMPONENTS_PHONEHUB_CAMERA_ROLL_THUMBNAIL_DECODER_IMPL_H_
#define ASH_COMPONENTS_PHONEHUB_CAMERA_ROLL_THUMBNAIL_DECODER_IMPL_H_

#include "ash/components/phonehub/camera_roll_thumbnail_decoder.h"

#include "ash/components/phonehub/proto/phonehub_api.pb.h"
#include "base/callback.h"
#include "base/memory/weak_ptr.h"
#include "services/data_decoder/public/cpp/data_decoder.h"
#include "services/data_decoder/public/cpp/decode_image.h"
#include "ui/gfx/image/image.h"

namespace chromeos {
namespace phonehub {

class CameraRollThumbnailDecoderImpl : public CameraRollThumbnailDecoder {
 public:
  CameraRollThumbnailDecoderImpl();
  ~CameraRollThumbnailDecoderImpl() override;

  void BatchDecode(const proto::FetchCameraRollItemsResponse& response,
                   const std::vector<CameraRollItem>& current_items,
                   base::OnceCallback<void(BatchDecodeResult result,
                                           const std::vector<CameraRollItem>&)>
                       callback) override;

 private:
  friend class CameraRollThumbnailDecoderImplTest;
  friend class FakeDecoderDelegate;

  // A pending request to decode the thumbnail of a camera roll item.
  class DecodeRequest {
   public:
    explicit DecodeRequest(const proto::CameraRollItem& item_proto);
    virtual ~DecodeRequest();

    const proto::CameraRollItemMetadata& GetMetadata() const;

    // Completes this request with thumbnail bitmap decoded from the raw bytes
    // in the proto.
    void CompleteWithDecodedBitmap(const SkBitmap& bitmap);
    // Completes this request with an existing image that has already been
    // decoded for the same item.
    void CompleteWithExistingImage(const gfx::Image& image);

    // Returns the encoded raw bytes of the thumbnail. May return an empty
    // string if the thumbnail is not sent with the camera roll item proto.
    const std::string& GetEncodedThumbnail() const;
    // Returns an empty image when the request is not completed or a non-empty
    // image when the requests is complete.
    const gfx::Image& decoded_thumbnail() const { return decoded_thumbnail_; }

   private:
    const proto::CameraRollItem item_proto_;
    gfx::Image decoded_thumbnail_;
  };

  // Delegate class that decodes camera roll item thumbnails. Can be overridden
  // in tests.
  class DecoderDelegate {
   public:
    DecoderDelegate();
    virtual ~DecoderDelegate();

    virtual void DecodeThumbnail(const DecodeRequest& request,
                                 data_decoder::DecodeImageCallback callback);

   private:
    // The instance of DataDecoder to decode thumbnail images. The underlying
    // service instance is started lazily when needed and torn down when not in
    // use.
    data_decoder::DataDecoder data_decoder_;
  };

  void OnThumbnailDecoded(const proto::CameraRollItemMetadata& item_metadata,
                          const SkBitmap& thumbnail_bitmap);
  // Checks whether all requests to decode thumbnails have been completed for
  // the latest items received, and invoke the pending callback if so.
  void CheckPendingThumbnailRequests();
  // Marks the pending requests as complete with the given result code and an
  // empty item list when the requests are cancelled or when an error occurs.
  void CompletePendingRequestsWithResult(
      CameraRollThumbnailDecoder::BatchDecodeResult result);

  std::unique_ptr<DecoderDelegate> decoder_delegate_;
  std::vector<DecodeRequest> pending_requests_;
  base::OnceCallback<void(BatchDecodeResult result,
                          const std::vector<CameraRollItem>&)>
      pending_callback_;
  // Contains weak pointers to callbacks passed to the |DecoderDelegate|.
  base::WeakPtrFactory<CameraRollThumbnailDecoderImpl> weak_ptr_factory_{this};
};

}  // namespace phonehub
}  // namespace chromeos

#endif  // ASH_COMPONENTS_PHONEHUB_CAMERA_ROLL_THUMBNAIL_DECODER_IMPL_H_
