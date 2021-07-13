/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file video_driver.hpp Base of all video drivers. */

#ifndef VIDEO_VIDEO_DRIVER_HPP
#define VIDEO_VIDEO_DRIVER_HPP

#include "../driver.h"
#include "../core/geometry_type.hpp"
#include "../core/math_func.hpp"
#include "../gfx_func.h"
#include "../settings_type.h"
#include "../zoom_type.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#include <functional>

extern std::string _ini_videodriver;
extern std::vector<Dimension> _resolutions;
extern Dimension _cur_resolution;
extern bool _rightclick_emulate;
extern bool _video_hw_accel;
extern bool _video_vsync;

/** The base of all video drivers. */
class VideoDriver : public Driver {
	const uint DEFAULT_WINDOW_WIDTH = 640u;  ///< Default window width.
	const uint DEFAULT_WINDOW_HEIGHT = 480u; ///< Default window height.

public:
	VideoDriver() : fast_forward_key_pressed(false), fast_forward_via_key(false), is_game_threaded(true) {}

	/**
	 * Mark a particular area dirty.
	 * @param left   The left most line of the dirty area.
	 * @param top    The top most line of the dirty area.
	 * @param width  The width of the dirty area.
	 * @param height The height of the dirty area.
	 */
	virtual void MakeDirty(int left, int top, int width, int height) = 0;

	/**
	 * Perform the actual drawing.
	 */
	virtual void MainLoop() = 0;

	/**
	 * Change the resolution of the window.
	 * @param w The new width.
	 * @param h The new height.
	 * @return True if the change succeeded.
	 */
	virtual bool ChangeResolution(int w, int h) = 0;

	/**
	 * Change the full screen setting.
	 * @param fullscreen The new setting.
	 * @return True if the change succeeded.
	 */
	virtual bool ToggleFullscreen(bool fullscreen) = 0;

	/**
	 * Change the vsync setting.
	 * @param vsync The new setting.
	 */
	virtual void ToggleVsync(bool vsync) {}

	/**
	 * Callback invoked after the blitter was changed.
	 * @return True if no error.
	 */
	virtual bool AfterBlitterChange()
	{
		return true;
	}

	virtual bool ClaimMousePointer()
	{
		return true;
	}

	/**
	 * Get whether the mouse cursor is drawn by the video driver.
	 * @return True if cursor drawing is done by the video driver.
	 */
	virtual bool UseSystemCursor()
	{
		return false;
	}

	/**
	 * Populate all sprites in cache.
	 */
	virtual void PopulateSystemSprites() {}

	/**
	 * Clear all cached sprites.
	 */
	virtual void ClearSystemSprites() {}

	/**
	 * Whether the driver has a graphical user interface with the end user.
	 * Or in other words, whether we should spawn a thread for world generation
	 * and NewGRF scanning so the graphical updates can keep coming. Otherwise
	 * progress has to be shown on the console, which uses by definition another
	 * thread/process for display purposes.
	 * @return True for all drivers except null and dedicated.
	 */
	virtual bool HasGUI() const
	{
		return true;
	}

	/**
	 * Has this video driver an efficient code path for palette animated 8-bpp sprites?
	 * @return True if the driver has an efficient code path for 8-bpp.
	 */
	virtual bool HasEfficient8Bpp() const
	{
		return false;
	}

	/**
	 * Does this video driver support a separate animation buffer in addition to the colour buffer?
	 * @return True if a separate animation buffer is supported.
	 */
	virtual bool HasAnimBuffer()
	{
		return false;
	}

	/**
	 * Get a pointer to the animation buffer of the video back-end.
	 * @return Pointer to the buffer or nullptr if no animation buffer is supported.
	 */
	virtual uint8 *GetAnimBuffer()
	{
		return nullptr;
	}

	virtual int GetAnimBufferPitch()
	{
		NOT_REACHED();
	}

	/**
	 * An edit box lost the input focus. Abort character compositing if necessary.
	 */
	virtual void EditBoxLostFocus() {}

	/**
	 * An edit box gained the input focus
	 */
	virtual void EditBoxGainedFocus() {}

	/**
	 * Get a list of refresh rates of each available monitor.
	 * @return A vector of the refresh rates of all available monitors.
	 */
	virtual std::vector<int> GetListOfMonitorRefreshRates()
	{
		return {};
	}

	/**
	 * Get a suggested default GUI zoom taking screen DPI into account.
	 */
	virtual ZoomLevel GetSuggestedUIZoom()
	{
		float dpi_scale = this->GetDPIScale();

		if (dpi_scale >= 3.0f) return ZOOM_LVL_NORMAL;
		if (dpi_scale >= 1.5f) return ZOOM_LVL_OUT_2X;
		return ZOOM_LVL_OUT_4X;
	}

	/**
	 * Queue a function to be called on the main thread with game state
	 * lock held and video buffer locked. Queued functions will be
	 * executed on the next draw tick.
	 * @param func Function to call.
	 */
	void QueueOnMainThread(std::function<void()> &&func)
	{
		std::lock_guard<std::mutex> lock(this->cmd_queue_mutex);

		this->cmd_queue.emplace_back(std::forward<std::function<void()>>(func));
	}

	void GameLoopPause();

	/**
	 * Get the currently active instance of the video driver.
	 */
	static VideoDriver *GetInstance() {
		return static_cast<VideoDriver*>(*DriverFactoryBase::GetActiveDriver(Driver::DT_VIDEO));
	}

	/**
	 * Helper struct to ensure the video buffer is locked and ready for drawing. The destructor
	 * will make sure the buffer is unlocked no matter how the scope is exited.
	 */
	struct VideoBufferLocker {
		VideoBufferLocker()
		{
			this->unlock = VideoDriver::GetInstance()->LockVideoBuffer();
		}

		~VideoBufferLocker()
		{
			if (this->unlock) VideoDriver::GetInstance()->UnlockVideoBuffer();
		}

	private:
		bool unlock; ///< Stores if the lock did anything that has to be undone.
	};

protected:
	const uint ALLOWED_DRIFT = 5; ///< How many times videodriver can miss deadlines without it being overly compensated.

	/**
	 * Get the resolution of the main screen.
	 */
	virtual Dimension GetScreenSize() const { return { DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT }; }

	/**
	 * Get DPI scaling factor of the screen OTTD is displayed on.
	 * @return 1.0 for default platform DPI, > 1.0 for higher DPI values, and < 1.0 for smaller DPI values.
	 */
	virtual float GetDPIScale() { return 1.0f; }

	/**
	 * Apply resolution auto-detection and clamp to sensible defaults.
	 */
	void UpdateAutoResolution()
	{
		if (_cur_resolution.width == 0 || _cur_resolution.height == 0) {
			/* Auto-detect a good resolution. We aim for 75% of the screen size.
			 * Limit width times height times bytes per pixel to fit a 32 bit
			 * integer, This way all internal drawing routines work correctly. */
			Dimension res = this->GetScreenSize();
			_cur_resolution.width  = ClampU(res.width  * 3 / 4, DEFAULT_WINDOW_WIDTH, UINT16_MAX / 2);
			_cur_resolution.height = ClampU(res.height * 3 / 4, DEFAULT_WINDOW_HEIGHT, UINT16_MAX / 2);
		}
	}

	/**
	 * Handle input logic, is CTRL pressed, should we fast-forward, etc.
	 */
	virtual void InputLoop() {}

	/**
	 * Make sure the video buffer is ready for drawing.
	 * @returns True if the video buffer has to be unlocked.
	 */
	virtual bool LockVideoBuffer() {
		return false;
	}

	/**
	 * Unlock a previously locked video buffer.
	 */
	virtual void UnlockVideoBuffer() {}

	/**
	 * Paint the window.
	 */
	virtual void Paint() {}

	/**
	 * Process any pending palette animation.
	 */
	virtual void CheckPaletteAnim() {}

	/**
	 * Process a single system event.
	 * @returns False if there are no more events to process.
	 */
	virtual bool PollEvent() { return false; };

	/**
	 * Start the loop for game-tick.
	 */
	void StartGameThread();

	/**
	 * Stop the loop for the game-tick. This can still tick at most one time before truly shutting down.
	 */
	void StopGameThread();

	/**
	 * Give the video-driver a tick.
	 * It will process any potential game-tick and/or draw-tick, and/or any
	 * other video-driver related event.
	 */
	void Tick();

	/**
	 * Sleep till the next tick is about to happen.
	 */
	void SleepTillNextTick();

	std::chrono::steady_clock::duration GetGameInterval()
	{
		/* If we are paused, run on normal speed. */
		if (_pause_mode) return std::chrono::milliseconds(MILLISECONDS_PER_TICK);
		/* Infinite speed, as quickly as you can. */
		if (_game_speed == 0) return std::chrono::microseconds(0);

		return std::chrono::microseconds(MILLISECONDS_PER_TICK * 1000 * 100 / _game_speed);
	}

	std::chrono::steady_clock::duration GetDrawInterval()
	{
		return std::chrono::microseconds(1000000 / _settings_client.gui.refresh_rate);
	}

	/** Execute all queued commands. */
	void DrainCommandQueue()
	{
		std::vector<std::function<void()>> cmds{};

		{
			/* Exchange queue with an empty one to limit the time we
			 * hold the mutex. This also ensures that queued functions can
			 * add new functions to the queue without everything blocking. */
			std::lock_guard<std::mutex> lock(this->cmd_queue_mutex);
			cmds.swap(this->cmd_queue);
		}

		for (auto &f : cmds) {
			f();
		}
	}

	std::chrono::steady_clock::time_point next_game_tick;
	std::chrono::steady_clock::time_point next_draw_tick;

	bool fast_forward_key_pressed; ///< The fast-forward key is being pressed.
	bool fast_forward_via_key; ///< The fast-forward was enabled by key press.

	bool is_game_threaded;
	std::thread game_thread;
	std::mutex game_state_mutex;
	std::mutex game_thread_wait_mutex;

	static void GameThreadThunk(VideoDriver *drv);

private:
	std::mutex cmd_queue_mutex;
	std::vector<std::function<void()>> cmd_queue;

	void GameLoop();
	void GameThread();
};

#endif /* VIDEO_VIDEO_DRIVER_HPP */
