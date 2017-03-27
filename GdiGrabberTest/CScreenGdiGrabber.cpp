#include "stdafx.h"
#include "CScreenGdiGrabber.h"
#include <MMSystem.h>

namespace MediaFileRecorder {

	CScreenGdiGrabber::CScreenGdiGrabber()
	{
		frame_rate_ = 15;
		grab_rect_.left = grab_rect_.top = 0;
		grab_rect_.right = GetSystemMetrics(SM_CXSCREEN);
		grab_rect_.bottom = GetSystemMetrics(SM_CYSCREEN);
		started_ = false;
		last_tick_count_ = 0;
		perf_freq_.QuadPart = 0;
	}

	CScreenGdiGrabber::~CScreenGdiGrabber()
	{
		if (started_)
		{
			StopGrab();
		}
	}

	void CScreenGdiGrabber::RegisterDataCb(IScreenGrabberDataCb* cb)
	{
		vec_data_cb_.push_back(cb);
	}

	void CScreenGdiGrabber::UnRegisterDataCb(IScreenGrabberDataCb* cb)
	{
		auto iter = std::find(vec_data_cb_.begin(), vec_data_cb_.end(), cb);
		if (iter != vec_data_cb_.end())
		{
			vec_data_cb_.erase(iter);
		}
	}

	void CScreenGdiGrabber::SetGrabRect(int left, int top, int right, int bottom)
	{
		grab_rect_.left = left;
		grab_rect_.top = top;
		grab_rect_.right = right;
		grab_rect_.bottom = bottom;
	}

	void CScreenGdiGrabber::SetGrabFrameRate(int frame_rate)
	{
		frame_rate_ = frame_rate;
	}

	bool CScreenGdiGrabber::StartGrab()
	{
		if (started_)
		{
			OutputDebugStringA("Already started");
			return false;
		}

		CalculateFrameIntervalTick();

		src_dc_ = GetDC(NULL);
		dst_dc_ = CreateCompatibleDC(src_dc_);

		bmi_.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi_.bmiHeader.biWidth = grab_rect_.right - grab_rect_.left;
		bmi_.bmiHeader.biHeight = -(grab_rect_.bottom - grab_rect_.top);
		bmi_.bmiHeader.biPlanes = 1;
		bmi_.bmiHeader.biBitCount = 24;
		bmi_.bmiHeader.biCompression = BI_RGB;
		bmi_.bmiHeader.biSizeImage = 0;
		bmi_.bmiHeader.biXPelsPerMeter = 0;
		bmi_.bmiHeader.biYPelsPerMeter = 0;
		bmi_.bmiHeader.biClrUsed = 0;
		bmi_.bmiHeader.biClrImportant = 0;

		hbmp_ = CreateDIBSection(dst_dc_, &bmi_, DIB_RGB_COLORS, &bmp_buffer_, NULL, 0);
		if (!hbmp_)
		{
			OutputDebugStringA("Create DIB section failed");
			return false;
		}

		SelectObject(dst_dc_, hbmp_);

		m_hGrabTimer = CreateWaitableTimer(NULL, FALSE, NULL);

		StartGrabThread();
		started_ = true;
		return true;
	}

	void CScreenGdiGrabber::StopGrab()
	{
		if (started_)
		{
			StopGrabThread();
			ReleaseDC(NULL, src_dc_);
			DeleteDC(dst_dc_);
			DeleteObject(hbmp_);
			started_ = false;
		}
	}

	void CScreenGdiGrabber::StartGrabThread()
	{
		run_ = true;
		grab_thread_.swap(std::thread(std::bind(&CScreenGdiGrabber::GrabThreadProc, this)));
		SetThreadPriority(grab_thread_.native_handle(), THREAD_PRIORITY_TIME_CRITICAL);
	}

	void CScreenGdiGrabber::StopGrabThread()
	{
		run_ = false;
		if (grab_thread_.joinable())
			grab_thread_.join();
	}

	void CScreenGdiGrabber::GrabThreadProc()
	{
		while (run_)
		{
			int width = grab_rect_.right - grab_rect_.left;
			int heigth = grab_rect_.bottom - grab_rect_.top;
			int64_t begin = timeGetTime();
			BitBlt(dst_dc_, 0, 0,
				width, heigth, src_dc_,
				grab_rect_.left, grab_rect_.top,
				SRCCOPY);

			int64_t duration = timeGetTime() - begin;

			int64_t interval_tick = GetCurrentTickCount() - last_tick_count_;
			if (interval_tick < frame_interval_tick_)
			{
				LARGE_INTEGER first_fire_time;
				first_fire_time.QuadPart = -((frame_interval_tick_ - interval_tick) * 10000000 / perf_freq_.QuadPart);
				SetWaitableTimer(m_hGrabTimer, &first_fire_time, 0, NULL, NULL, FALSE);
				WaitForSingleObject(m_hGrabTimer, INFINITE);
				CancelWaitableTimer(m_hGrabTimer);
				/*Sleep(5);
				interval_tick = GetCurrentTickCount() - last_tick_count_;*/
			}

			char log[128] = { 0 };
			_snprintf(log, 128, "required interval: %lld, interval: %lld, bitblt time: %lld \n",
				frame_interval_tick_ * 1000 / perf_freq_.QuadPart,
				(GetCurrentTickCount() - last_tick_count_) * 1000 / perf_freq_.QuadPart,
				duration);
			OutputDebugStringA(log);

			last_tick_count_ = GetCurrentTickCount();

			for (IScreenGrabberDataCb* cb : vec_data_cb_)
			{
				cb->OnScreenData(bmp_buffer_, width, heigth, PIX_FMT::PIX_FMT_BGR24);
			}
		}
	}

	int64_t CScreenGdiGrabber::GetCurrentTickCount()
	{
		LARGE_INTEGER current_counter;
		QueryPerformanceCounter(&current_counter);
		return current_counter.QuadPart;
	}

	void CScreenGdiGrabber::CalculateFrameIntervalTick()
	{
		if (perf_freq_.QuadPart == 0)
		{
			QueryPerformanceFrequency(&perf_freq_);
		}

		frame_interval_tick_ = perf_freq_.QuadPart / frame_rate_;
	}

}