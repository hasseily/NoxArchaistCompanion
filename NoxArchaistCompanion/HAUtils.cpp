#include "pch.h"
#include "HAUtils.h"

#include <wincodec.h>
#include "ReadData.h"
#include "FindMedia.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace HA
{

    std::vector<uint8_t> LoadBGRAImage(const wchar_t* filename, uint32_t& width, uint32_t& height)
    {
        ComPtr<IWICImagingFactory> wicFactory;
        DX::ThrowIfFailed(CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory)));

        ComPtr<IWICBitmapDecoder> decoder;
        DX::ThrowIfFailed(wicFactory->CreateDecoderFromFilename(filename, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf()));

        ComPtr<IWICBitmapFrameDecode> frame;
        DX::ThrowIfFailed(decoder->GetFrame(0, frame.GetAddressOf()));

        DX::ThrowIfFailed(frame->GetSize(&width, &height));

        WICPixelFormatGUID pixelFormat;
        DX::ThrowIfFailed(frame->GetPixelFormat(&pixelFormat));

        uint32_t rowPitch = width * sizeof(uint32_t);
        uint32_t imageSize = rowPitch * height;

        std::vector<uint8_t> image;
        image.resize(size_t(imageSize));

        if (memcmp(&pixelFormat, &GUID_WICPixelFormat32bppBGRA, sizeof(GUID)) == 0)
        {
            DX::ThrowIfFailed(frame->CopyPixels(nullptr, rowPitch, imageSize, reinterpret_cast<BYTE*>(image.data())));
        }
        else
        {
            ComPtr<IWICFormatConverter> formatConverter;
            DX::ThrowIfFailed(wicFactory->CreateFormatConverter(formatConverter.GetAddressOf()));

            BOOL canConvert = FALSE;
            DX::ThrowIfFailed(formatConverter->CanConvert(pixelFormat, GUID_WICPixelFormat32bppBGRA, &canConvert));
            if (!canConvert)
            {
                throw std::exception("CanConvert");
            }

            DX::ThrowIfFailed(formatConverter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA,
                WICBitmapDitherTypeErrorDiffusion, nullptr, 0, WICBitmapPaletteTypeMedianCut));

            DX::ThrowIfFailed(formatConverter->CopyPixels(nullptr, rowPitch, imageSize, reinterpret_cast<BYTE*>(image.data())));
        }

        return image;
    }


    size_t ConvertWStrToStr(const std::wstring* wstr, std::string* str)
    {
        constexpr int MAX_WSTR_CONVERSION_LENGTH = 4096;
        size_t maxLength = wstr->length();
        if (maxLength > MAX_WSTR_CONVERSION_LENGTH)
            maxLength = MAX_WSTR_CONVERSION_LENGTH;
		size_t numConverted = 0;
        char szStr[MAX_WSTR_CONVERSION_LENGTH] = "";
		wcstombs_s(&numConverted, szStr, wstr->c_str(), maxLength);
        str->assign(szStr);
        return numConverted;
    }

	size_t ConvertStrToWStr(const std::string* str, std::wstring* wstr)
	{
		constexpr int MAX_STR_CONVERSION_LENGTH = 4096;
		size_t maxLength = str->length();
		if (maxLength > MAX_STR_CONVERSION_LENGTH)
			maxLength = MAX_STR_CONVERSION_LENGTH;
		size_t numConverted = 0;
		wchar_t wzStr[MAX_STR_CONVERSION_LENGTH] = L"";
		mbstowcs_s(&numConverted, wzStr, str->c_str(), maxLength);
		wstr->assign(wzStr);
		return numConverted;
	}
}