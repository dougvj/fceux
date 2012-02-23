/* ---------------------------------------------------------------------------------
Implementation file of TASEDITOR_PROJECT class
Copyright (c) 2011-2012 AnS

(The MIT License)
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
------------------------------------------------------------------------------------
Project - Header of working project
[Singleton]

* stores the info about current project filename and about having unsaved changes
* implements saving and loading project files from filesystem
* implements autosave function
* stores resources: autosave period scale, default filename
------------------------------------------------------------------------------------ */

#include "taseditor_project.h"
#include "utils/xstring.h"
#include "version.h"

extern TASEDITOR_CONFIG taseditor_config;
extern TASEDITOR_WINDOW taseditor_window;
extern MARKERS_MANAGER markers_manager;
extern BOOKMARKS bookmarks;
extern POPUP_DISPLAY popup_display;
extern GREENZONE greenzone;
extern PLAYBACK playback;
extern RECORDER recorder;
extern HISTORY history;
extern PIANO_ROLL piano_roll;
extern SELECTION selection;
extern SPLICER splicer;

extern void FCEU_PrintError(char *format, ...);
extern bool SaveProject();
extern bool SaveProjectAs();
extern int GetInputType(MovieData& md);
extern void SetInputType(MovieData& md, int new_input_type);

TASEDITOR_PROJECT::TASEDITOR_PROJECT()
{
}

void TASEDITOR_PROJECT::init()
{
	// default filename for a new project is blank
	projectFile = "";
	projectName = "";
	fm2FileName = "";
	reset();
}
void TASEDITOR_PROJECT::reset()
{
	changed = false;
}
void TASEDITOR_PROJECT::update()
{
	// if it's time to autosave - pop Save As dialog
	if (changed && taseditor_config.autosave_period && clock() >= next_save_shedule)
	{
		if (taseditor_config.silent_autosave)
			SaveProject();
		else
			SaveProjectAs();
		// in case user pressed Cancel, postpone saving to next time
		SheduleNextAutosave();
	}
	
}

bool TASEDITOR_PROJECT::save()
{
	std::string PFN = GetProjectFile();
	if (PFN.empty()) return false;
	const char* filename = PFN.c_str();
	EMUFILE_FILE* ofs = FCEUD_UTF8_fstream(filename, "wb");
	
	currMovieData.loadFrameCount = currMovieData.records.size();
	currMovieData.dump(ofs, true);

	// save all modules
	unsigned int saved_stuff = ALL_SAVED;
	write32le(saved_stuff, ofs);
	markers_manager.save(ofs);
	bookmarks.save(ofs);
	greenzone.save(ofs);
	history.save(ofs);
	piano_roll.save(ofs);
	selection.save(ofs);

	delete ofs;

	playback.updateProgressbar();
	this->reset();
	return true;
}
bool TASEDITOR_PROJECT::save_compact(char* filename, bool save_binary, bool save_markers, bool save_bookmarks, bool save_greenzone, bool save_history, bool save_piano_roll, bool save_selection)
{
	EMUFILE_FILE* ofs = FCEUD_UTF8_fstream(filename, "wb");
	
	currMovieData.loadFrameCount = currMovieData.records.size();
	currMovieData.dump(ofs, save_binary);

	// save specified modules
	unsigned int saved_stuff = 0;
	if (save_markers) saved_stuff |= MARKERS_SAVED;
	if (save_bookmarks) saved_stuff |= BOOKMARKS_SAVED;
	if (save_greenzone) saved_stuff |= GREENZONE_SAVED;
	if (save_history) saved_stuff |= HISTORY_SAVED;
	if (save_piano_roll) saved_stuff |= PIANO_ROLL_SAVED;
	if (save_selection) saved_stuff |= SELECTION_SAVED;
	write32le(saved_stuff, ofs);
	markers_manager.save(ofs, save_markers);
	bookmarks.save(ofs, save_bookmarks);
	greenzone.save(ofs, save_greenzone);
	history.save(ofs, save_history);
	piano_roll.save(ofs, save_piano_roll);
	selection.save(ofs, save_selection);

	delete ofs;

	playback.updateProgressbar();
	return true;
}
bool TASEDITOR_PROJECT::load(char* fullname)
{
	EMUFILE_FILE ifs(fullname, "rb");

	if(ifs.fail())
	{
		FCEU_PrintError("Error opening %s!", fullname);
		return false;
	}

	FCEU_printf("\nLoading TAS Editor project %s...\n", fullname);

	MovieData tempMovieData = MovieData();
	extern bool LoadFM2(MovieData& movieData, EMUFILE* fp, int size, bool stopAfterHeader);
	if (LoadFM2(tempMovieData, &ifs, ifs.size(), false))
	{
		currMovieData = tempMovieData;
		currMovieData.emuVersion = FCEU_VERSION_NUMERIC;
		LoadSubtitles(currMovieData);
	} else
	{
		FCEU_PrintError("Error loading movie data from %s!", fullname);
		// do not load the project
		return false;
	}

	// ensure that movie has correct set of ports/fourscore
	SetInputType(currMovieData, GetInputType(currMovieData));

	// load modules
	unsigned int saved_stuff;
	read32le(&saved_stuff, &ifs);
	markers_manager.load(&ifs);
	bookmarks.load(&ifs);
	greenzone.load(&ifs);
	history.load(&ifs);
	piano_roll.load(&ifs);
	selection.load(&ifs);
	
	// reset other modules
	playback.reset();
	recorder.reset();
	splicer.reset();
	popup_display.reset();
	reset();
	RenameProject(fullname);
	return true;
}

void TASEDITOR_PROJECT::RenameProject(char* new_fullname)
{
	projectFile = new_fullname;
	char drv[512], dir[512], name[512], ext[512];		// For getting the filename
	splitpath(new_fullname, drv, dir, name, ext);
	projectName = name;
	std::string thisfm2name = name;
	thisfm2name.append(".fm2");
	fm2FileName = thisfm2name;
}
// -----------------------------------------------------------------
std::string TASEDITOR_PROJECT::GetProjectFile()
{
	return projectFile;
}
std::string TASEDITOR_PROJECT::GetProjectName()
{
	return projectName;
}
std::string TASEDITOR_PROJECT::GetFM2Name()
{
	return fm2FileName;
}

void TASEDITOR_PROJECT::SetProjectChanged()
{
	if (!changed)
	{
		changed = true;
		taseditor_window.UpdateCaption();
		SheduleNextAutosave();
	}
}
bool TASEDITOR_PROJECT::GetProjectChanged()
{
	return changed;
}

void TASEDITOR_PROJECT::SheduleNextAutosave()
{
	if (taseditor_config.autosave_period)
		next_save_shedule = clock() + taseditor_config.autosave_period * AUTOSAVE_PERIOD_SCALE;
}
