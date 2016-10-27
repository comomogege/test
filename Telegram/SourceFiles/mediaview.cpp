/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#include "stdafx.h"
#include "lang.h"

#include "mediaview.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "application.h"
#include "ui/filedialog.h"
#include "ui/popupmenu.h"
#include "media/media_clip_reader.h"
#include "media/view/media_clip_controller.h"
#include "styles/style_mediaview.h"
#include "media/media_audio.h"
#include "history/history_media_types.h"

namespace {

class SaveMsgClickHandler : public ClickHandler {
public:

	SaveMsgClickHandler(MediaView *view) : _view(view) {
	}

	void onClick(Qt::MouseButton button) const {
		if (button == Qt::LeftButton) {
			_view->showSaveMsgFile();
		}
	}

private:

	MediaView *_view;
};

TextParseOptions _captionTextOptions = {
	TextParseLinks | TextParseMentions | TextParseHashtags | TextParseMultiline | TextParseRichText, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};
TextParseOptions _captionBotOptions = {
	TextParseLinks | TextParseMentions | TextParseHashtags | TextParseMultiline | TextParseRichText | TextParseBotCommands, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

bool typeHasMediaOverview(MediaOverviewType type) {
	switch (type) {
	case OverviewPhotos:
	case OverviewVideos:
	case OverviewMusicFiles:
	case OverviewFiles:
	case OverviewVoiceFiles:
	case OverviewLinks: return true;
	default: break;
	}
	return false;
}

} // namespace

MediaView::MediaView() : TWidget(App::wnd())
, _animStarted(getms())
, _docDownload(this, lang(lng_media_download), st::mvDocLink)
, _docSaveAs(this, lang(lng_mediaview_save_as), st::mvDocLink)
, _docCancel(this, lang(lng_cancel), st::mvDocLink)
, _radial(animation(this, &MediaView::step_radial))
, _lastAction(-st::mvDeltaFromLastAction, -st::mvDeltaFromLastAction)
, _a_state(animation(this, &MediaView::step_state))
, _dropdown(this, st::mvDropdown) {
	TextCustomTagsMap custom;
	custom.insert(QChar('c'), qMakePair(textcmdStartLink(1), textcmdStopLink()));
	_saveMsgText.setRichText(st::medviewSaveMsgFont, lang(lng_mediaview_saved), _textDlgOptions, custom);
	_saveMsg = QRect(0, 0, _saveMsgText.maxWidth() + st::medviewSaveMsgPadding.left() + st::medviewSaveMsgPadding.right(), st::medviewSaveMsgFont->height + st::medviewSaveMsgPadding.top() + st::medviewSaveMsgPadding.bottom());
	_saveMsgText.setLink(1, MakeShared<SaveMsgClickHandler>(this));

	connect(QApplication::desktop(), SIGNAL(resized(int)), this, SLOT(onScreenResized(int)));

	subscribe(FileDownload::ImageLoaded(), [this] {
		if (!isHidden()) {
			updateControls();
		}
	});

	generateTransparentBrush();

	setWindowFlags(Qt::FramelessWindowHint | Qt::BypassWindowManagerHint | Qt::Tool | Qt::NoDropShadowWindowHint);
	moveToScreen();
	setAttribute(Qt::WA_NoSystemBackground, true);
	setAttribute(Qt::WA_TranslucentBackground, true);
	setMouseTracking(true);

	hide();
	createWinId();
	if (cPlatform() == dbipWindows) {
		setWindowState(Qt::WindowFullScreen);
	}

	_saveMsgUpdater.setSingleShot(true);
	connect(&_saveMsgUpdater, SIGNAL(timeout()), this, SLOT(updateImage()));

	connect(App::wnd()->windowHandle(), SIGNAL(activeChanged()), this, SLOT(onCheckActive()));

	setAttribute(Qt::WA_AcceptTouchEvents);
	_touchTimer.setSingleShot(true);
	connect(&_touchTimer, SIGNAL(timeout()), this, SLOT(onTouchTimer()));

	_btns.push_back(_btnSaveCancel = _dropdown.addButton(new IconedButton(this, st::mvButton, lang(lng_cancel))));
	connect(_btnSaveCancel, SIGNAL(clicked()), this, SLOT(onSaveCancel()));
	_btns.push_back(_btnToMessage = _dropdown.addButton(new IconedButton(this, st::mvButton, lang(lng_context_to_msg))));
	connect(_btnToMessage, SIGNAL(clicked()), this, SLOT(onToMessage()));
	_btns.push_back(_btnShowInFolder = _dropdown.addButton(new IconedButton(this, st::mvButton, lang((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_context_show_in_finder : lng_context_show_in_folder))));
	connect(_btnShowInFolder, SIGNAL(clicked()), this, SLOT(onShowInFolder()));
	_btns.push_back(_btnCopy = _dropdown.addButton(new IconedButton(this, st::mvButton, lang(lng_mediaview_copy))));
	connect(_btnCopy, SIGNAL(clicked()), this, SLOT(onCopy()));
	_btns.push_back(_btnForward = _dropdown.addButton(new IconedButton(this, st::mvButton, lang(lng_mediaview_forward))));
	connect(_btnForward, SIGNAL(clicked()), this, SLOT(onForward()));
	_btns.push_back(_btnDelete = _dropdown.addButton(new IconedButton(this, st::mvButton, lang(lng_mediaview_delete))));
	connect(_btnDelete, SIGNAL(clicked()), this, SLOT(onDelete()));
	_btns.push_back(_btnSaveAs = _dropdown.addButton(new IconedButton(this, st::mvButton, lang(lng_mediaview_save_as))));
	connect(_btnSaveAs, SIGNAL(clicked()), this, SLOT(onSaveAs()));
	_btns.push_back(_btnViewAll = _dropdown.addButton(new IconedButton(this, st::mvButton, lang(lng_mediaview_photos_all))));
	connect(_btnViewAll, SIGNAL(clicked()), this, SLOT(onOverview()));

	_dropdown.hide();
	connect(&_dropdown, SIGNAL(hiding()), this, SLOT(onDropdownHiding()));

	_controlsHideTimer.setSingleShot(true);
	connect(&_controlsHideTimer, SIGNAL(timeout()), this, SLOT(onHideControls()));

	connect(&_docDownload, SIGNAL(clicked()), this, SLOT(onDownload()));
	connect(&_docSaveAs, SIGNAL(clicked()), this, SLOT(onSaveAs()));
	connect(&_docCancel, SIGNAL(clicked()), this, SLOT(onSaveCancel()));
}

void MediaView::moveToScreen() {
	if (App::wnd() && windowHandle() && App::wnd()->windowHandle() && windowHandle()->screen() != App::wnd()->windowHandle()->screen()) {
		windowHandle()->setScreen(App::wnd()->windowHandle()->screen());
	}

	QPoint wndCenter(App::wnd()->x() + App::wnd()->width() / 2, App::wnd()->y() + App::wnd()->height() / 2);
	QRect avail = Sandbox::screenGeometry(wndCenter);
	if (avail != geometry()) {
		setGeometry(avail);
	}

	int32 navSkip = 2 * st::mvControlMargin + st::mvControlSize;
	_closeNav = myrtlrect(width() - st::mvControlMargin - st::mvControlSize, st::mvControlMargin, st::mvControlSize, st::mvControlSize);
	_closeNavIcon = centerrect(_closeNav, st::mediaviewClose);
	_leftNav = myrtlrect(st::mvControlMargin, navSkip, st::mvControlSize, height() - 2 * navSkip);
	_leftNavIcon = centerrect(_leftNav, st::mediaviewLeft);
	_rightNav = myrtlrect(width() - st::mvControlMargin - st::mvControlSize, navSkip, st::mvControlSize, height() - 2 * navSkip);
	_rightNavIcon = centerrect(_rightNav, st::mediaviewRight);

	_saveMsg.moveTo((width() - _saveMsg.width()) / 2, (height() - _saveMsg.height()) / 2);
}

void MediaView::mediaOverviewUpdated(PeerData *peer, MediaOverviewType type) {
	if (!_photo && !_doc) return;
	if (_photo && _overview == OverviewChatPhotos && _history && !_history->peer->isUser()) {
		auto lastChatPhoto = computeLastOverviewChatPhoto();
		if (_index < 0 && _photo == lastChatPhoto.photo && _photo == _additionalChatPhoto) {
			auto firstOpened = _firstOpenedPeerPhoto;
			showPhoto(_photo, lastChatPhoto.item);
			_firstOpenedPeerPhoto = firstOpened;
			return;
		}
		computeAdditionalChatPhoto(_history->peer, lastChatPhoto.photo);
	}

	if (_history && (_history->peer == peer || (_migrated && _migrated->peer == peer)) && type == _overview && _msgid) {
		_index = -1;
		if (_msgmigrated) {
			for (int i = 0, l = _migrated->overview[_overview].size(); i < l; ++i) {
				if (_migrated->overview[_overview].at(i) == _msgid) {
					_index = i;
					break;
				}
			}
		} else {
			for (int i = 0, l = _history->overview[_overview].size(); i < l; ++i) {
				if (_history->overview[_overview].at(i) == _msgid) {
					_index = i;
					break;
				}
			}
		}
		updateControls();
		preloadData(0);
	} else if (_user == peer && type == OverviewCount) {
		if (!_photo) return;

		_index = -1;
		for (int i = 0, l = _user->photos.size(); i < l; ++i) {
			if (_user->photos.at(i) == _photo) {
				_index = i;
				break;
			}
		}
		updateControls();
		preloadData(0);
	}
}

bool MediaView::fileShown() const {
	return !_current.isNull() || gifShown();
}

bool MediaView::gifShown() const {
	if (_gif && _gif->ready()) {
		if (!_gif->started()) {
			if (_doc->isVideo() && _autoplayVideoDocument != _doc && !_gif->videoPaused()) {
				_gif->pauseResumeVideo();
				const_cast<MediaView*>(this)->_videoPaused = _gif->videoPaused();
			}
			_gif->start(_gif->width(), _gif->height(), _gif->width(), _gif->height(), ImageRoundRadius::None);
			const_cast<MediaView*>(this)->_current = QPixmap();
		}
		return true;// _gif->state() != Media::Clip::State::Error;
	}
	return false;
}

void MediaView::stopGif() {
	_gif = nullptr;
	_videoPaused = _videoStopped = _videoIsSilent = false;
	_fullScreenVideo = false;
	_clipController.destroy();
	if (audioPlayer()) {
		disconnect(audioPlayer(), SIGNAL(updated(const AudioMsgId&)), this, SLOT(onVideoPlayProgress(const AudioMsgId&)));
	}
}

void MediaView::documentUpdated(DocumentData *doc) {
	if (_doc && _doc == doc && !fileShown()) {
		if ((_doc->loading() && _docCancel.isHidden()) || (!_doc->loading() && !_docCancel.isHidden())) {
			updateControls();
		} else if (_doc->loading()) {
			updateDocSize();
			update(_docRect);
		}
	}
}

void MediaView::changingMsgId(HistoryItem *row, MsgId newId) {
	if (row->id == _msgid) {
		_msgid = newId;
	}
	mediaOverviewUpdated(row->history()->peer, _overview);
}

void MediaView::updateDocSize() {
	if (!_doc || fileShown()) return;

	if (_doc->loading()) {
		quint64 ready = _doc->loadOffset(), total = _doc->size;
		QString readyStr, totalStr, mb;
		if (total >= 1024 * 1024) { // more than 1 mb
			qint64 readyTenthMb = (ready * 10 / (1024 * 1024)), totalTenthMb = (total * 10 / (1024 * 1024));
			readyStr = QString::number(readyTenthMb / 10) + '.' + QString::number(readyTenthMb % 10);
			totalStr = QString::number(totalTenthMb / 10) + '.' + QString::number(totalTenthMb % 10);
			mb = qsl("MB");
		} else if (total >= 1024) {
			qint64 readyKb = (ready / 1024), totalKb = (total / 1024);
			readyStr = QString::number(readyKb);
			totalStr = QString::number(totalKb);
			mb = qsl("KB");
		} else {
			readyStr = QString::number(ready);
			totalStr = QString::number(total);
			mb = qsl("B");
		}
		_docSize = lng_media_save_progress(lt_ready, readyStr, lt_total, totalStr, lt_mb, mb);
	} else {
		_docSize = formatSizeText(_doc->size);
	}
	_docSizeWidth = st::mvFont->width(_docSize);
	int32 maxw = st::mvDocSize.width() - st::mvDocIconSize - st::mvDocPadding * 3;
	if (_docSizeWidth > maxw) {
		_docSize = st::mvFont->elided(_docSize, maxw);
		_docSizeWidth = st::mvFont->width(_docSize);
	}
}

void MediaView::updateControls() {
	if (_doc && !fileShown()) {
		if (_doc->loading()) {
			_docDownload.hide();
			_docSaveAs.hide();
			_docCancel.moveToLeft(_docRect.x() + 2 * st::mvDocPadding + st::mvDocIconSize, _docRect.y() + st::mvDocPadding + st::mvDocLinksTop);
			_docCancel.show();
		} else {
			if (_doc->loaded(DocumentData::FilePathResolveChecked)) {
				_docDownload.hide();
				_docSaveAs.moveToLeft(_docRect.x() + 2 * st::mvDocPadding + st::mvDocIconSize, _docRect.y() + st::mvDocPadding + st::mvDocLinksTop);
				_docSaveAs.show();
				_docCancel.hide();
			} else {
				_docDownload.moveToLeft(_docRect.x() + 2 * st::mvDocPadding + st::mvDocIconSize, _docRect.y() + st::mvDocPadding + st::mvDocLinksTop);
				_docDownload.show();
				_docSaveAs.moveToLeft(_docRect.x() + 2.5 * st::mvDocPadding + st::mvDocIconSize + _docDownload.width(), _docRect.y() + st::mvDocPadding + st::mvDocLinksTop);
				_docSaveAs.show();
				_docCancel.hide();
			}
		}
		updateDocSize();
	} else {
		_docDownload.hide();
		_docSaveAs.hide();
		_docCancel.hide();
	}
	radialStart();

	_saveVisible = ((_photo && _photo->loaded()) || (_doc && (_doc->loaded(DocumentData::FilePathResolveChecked) || (!fileShown() && (_photo || _doc)))));
	_saveNav = myrtlrect(width() - st::mvIconSize.width() * 2, height() - st::mvIconSize.height(), st::mvIconSize.width(), st::mvIconSize.height());
	_saveNavIcon = centerrect(_saveNav, st::mediaviewSave);
	_moreNav = myrtlrect(width() - st::mvIconSize.width(), height() - st::mvIconSize.height(), st::mvIconSize.width(), st::mvIconSize.height());
	_moreNavIcon = centerrect(_moreNav, st::mediaviewMore);

	QDateTime d, dNow(date(unixtime()));
	if (_photo) {
		d = date(_photo->date);
	} else if (_doc) {
		d = date(_doc->date);
	} else if (HistoryItem *item = App::histItemById(_msgmigrated ? 0 : _channel, _msgid)) {
		d = item->date;
	}
	if (d.date() == dNow.date()) {
		_dateText = lng_mediaview_today(lt_time, d.time().toString(cTimeFormat()));
	} else if (d.date().addDays(1) == dNow.date()) {
		_dateText = lng_mediaview_yesterday(lt_time, d.time().toString(cTimeFormat()));
	} else {
		_dateText = lng_mediaview_date_time(lt_date, d.date().toString(qsl("dd.MM.yy")), lt_time, d.time().toString(cTimeFormat()));
	}
	if (_from) {
		_fromName.setText(st::mvFont, (_from->migrateTo() ? _from->migrateTo() : _from)->name, _textNameOptions);
		_nameNav = myrtlrect(st::mvTextLeft, height() - st::mvTextTop, qMin(_fromName.maxWidth(), width() / 3), st::mvFont->height);
		_dateNav = myrtlrect(st::mvTextLeft + _nameNav.width() + st::mvTextSkip, height() - st::mvTextTop, st::mvFont->width(_dateText), st::mvFont->height);
	} else {
		_nameNav = QRect();
		_dateNav = myrtlrect(st::mvTextLeft, height() - st::mvTextTop, st::mvFont->width(_dateText), st::mvFont->height);
	}
	updateHeader();
	if (_photo || (_history && (_overview == OverviewPhotos || _overview == OverviewChatPhotos || _overview == OverviewFiles || _overview == OverviewVideos))) {
		_leftNavVisible = (_index > 0) || (_index == 0 && (
			(!_msgmigrated && _history && _history->overview[_overview].size() < _history->overviewCount(_overview)) ||
			(_msgmigrated && _migrated && _migrated->overview[_overview].size() < _migrated->overviewCount(_overview)) ||
			(!_msgmigrated && _history && _migrated && (!_migrated->overview[_overview].isEmpty() || _migrated->overviewCount(_overview) > 0)))) ||
			(_index < 0 && _photo == _additionalChatPhoto &&
				((_history && _history->overviewCount(_overview) > 0) ||
				(_migrated && _history->overviewLoaded(_overview) && _migrated->overviewCount(_overview) > 0))
			);
		_rightNavVisible = (_index >= 0) && (
			(!_msgmigrated && _history && _index + 1 < _history->overview[_overview].size()) ||
			(_msgmigrated && _migrated && _index + 1 < _migrated->overview[_overview].size()) ||
			(_msgmigrated && _migrated && _history && (!_history->overview[_overview].isEmpty() || _history->overviewCount(_overview) > 0)) ||
			(!_msgmigrated && _history && _index + 1 == _history->overview[_overview].size() && _additionalChatPhoto) ||
			(_msgmigrated && _migrated && _index + 1 == _migrated->overview[_overview].size() && _history->overviewCount(_overview) == 0 && _additionalChatPhoto) ||
			(!_history && _user && (_index + 1 < _user->photos.size() || _index + 1 < _user->photosCount)));
		if (_msgmigrated && !_history->overviewLoaded(_overview)) {
			_leftNavVisible = _rightNavVisible = false;
		}
	} else {
		_leftNavVisible = _rightNavVisible = false;
	}

	if (!_caption.isEmpty()) {
		int32 skipw = qMax(_dateNav.left() + _dateNav.width(), _headerNav.left() + _headerNav.width());
		int32 maxw = qMin(qMax(width() - 2 * skipw - st::mvCaptionPadding.left() - st::mvCaptionPadding.right() - 2 * st::mvCaptionMargin.width(), int(st::msgMinWidth)), _caption.maxWidth());
		int32 maxh = qMin(_caption.countHeight(maxw), int(height() / 4 - st::mvCaptionPadding.top() - st::mvCaptionPadding.bottom() - 2 * st::mvCaptionMargin.height()));
		_captionRect = QRect((width() - maxw) / 2, height() - maxh - st::mvCaptionPadding.bottom() - st::mvCaptionMargin.height(), maxw, maxh);
	} else {
		_captionRect = QRect();
	}
	if (_clipController) {
		setClipControllerGeometry();
	}
	updateOver(mapFromGlobal(QCursor::pos()));
	update();
}

void MediaView::updateDropdown() {
	_btnSaveCancel->setVisible(_doc && _doc->loading());
	_btnToMessage->setVisible(_msgid > 0);
	_btnShowInFolder->setVisible(_doc && !_doc->filepath(DocumentData::FilePathResolveChecked).isEmpty());
	_btnSaveAs->setVisible(true);
	_btnCopy->setVisible((_doc && fileShown()) || (_photo && _photo->loaded()));
	_btnForward->setVisible(_canForward);
	_btnDelete->setVisible(_canDelete || (_photo && App::self() && _user == App::self()) || (_photo && _photo->peer && _photo->peer->photoId == _photo->id && (_photo->peer->isChat() || (_photo->peer->isChannel() && _photo->peer->asChannel()->amCreator()))));
	_btnViewAll->setVisible(_history && typeHasMediaOverview(_overview));
	_btnViewAll->setText(lang(_doc ? lng_mediaview_files_all : lng_mediaview_photos_all));
	_dropdown.updateButtons();
	_dropdown.moveToRight(0, height() - _dropdown.height());
}

void MediaView::step_state(uint64 ms, bool timer) {
	bool result = false;
	for (Showing::iterator i = _animations.begin(); i != _animations.end();) {
		int64 start = i.value();
		switch (i.key()) {
		case OverLeftNav: update(_leftNav); break;
		case OverRightNav: update(_rightNav); break;
		case OverName: update(_nameNav); break;
		case OverDate: update(_dateNav); break;
		case OverHeader: update(_headerNav); break;
		case OverClose: update(_closeNav); break;
		case OverSave: update(_saveNav); break;
		case OverIcon: update(_docIconRect); break;
		case OverMore: update(_moreNav); break;
		default: break;
		}
		float64 dt = float64(ms - start) / st::mvFadeDuration;
		if (dt >= 1) {
			_animOpacities.remove(i.key());
			i = _animations.erase(i);
		} else {
			_animOpacities[i.key()].update(dt, anim::linear);
			++i;
		}
	}
	if (_controlsState == ControlsShowing || _controlsState == ControlsHiding) {
		float64 dt = float64(ms - _controlsAnimStarted) / (_controlsState == ControlsShowing ? st::mvShowDuration : st::mvHideDuration);
		if (dt >= 1) {
			a_cOpacity.finish();
			_controlsState = (_controlsState == ControlsShowing ? ControlsShown : ControlsHidden);
			updateCursor();
		} else {
			a_cOpacity.update(dt, anim::linear);
		}
		QRegion toUpdate = QRegion() + (_over == OverLeftNav ? _leftNav : _leftNavIcon) + (_over == OverRightNav ? _rightNav : _rightNavIcon) + (_over == OverClose ? _closeNav : _closeNavIcon) + _saveNavIcon + _moreNavIcon + _headerNav + _nameNav + _dateNav + _captionRect.marginsAdded(st::mvCaptionPadding);
		update(toUpdate);
		if (dt < 1) result = true;
	}
	if (!result && _animations.isEmpty()) {
		_a_state.stop();
	}
}

void MediaView::updateCursor() {
	setCursor(_controlsState == ControlsHidden ? Qt::BlankCursor : (_over == OverNone ? style::cur_default : style::cur_pointer));
}

float64 MediaView::radialProgress() const {
	if (_doc) {
		return _doc->progress();
	} else if (_photo) {
		return _photo->full->progress();
	}
	return 1.;
}

bool MediaView::radialLoading() const {
	if (_doc) {
		return _doc->loading();
	} else if (_photo) {
		return _photo->full->loading();
	}
	return false;
}

QRect MediaView::radialRect() const {
	if (_doc) {
		return _docIconRect;
	} else if (_photo) {
		return _photoRadialRect;
	}
	return QRect();
}

void MediaView::radialStart() {
	if (radialLoading() && !_radial.animating()) {
		_radial.start(radialProgress());
		if (auto shift = radialTimeShift()) {
			_radial.update(radialProgress(), !radialLoading(), getms() + shift);
		}
	}
}

uint64 MediaView::radialTimeShift() const {
	return _photo ? st::radialDuration : 0;
}

void MediaView::step_radial(uint64 ms, bool timer) {
	if (!_doc && !_photo) {
		_radial.stop();
		return;
	}
	_radial.update(radialProgress(), !radialLoading(), ms + radialTimeShift());
	if (timer && _radial.animating()) {
		update(radialRect());
	}
	if (_doc && _doc->loaded() && _doc->size < MediaViewImageSizeLimit && (!_radial.animating() || _doc->isAnimation() || _doc->isVideo())) {
		if (_doc->isVideo()) {
			_autoplayVideoDocument = _doc;
		}
		if (!_doc->data().isEmpty() && (_doc->isAnimation() || _doc->isVideo())) {
			displayDocument(_doc, App::histItemById(_msgmigrated ? 0 : _channel, _msgid));
		} else {
			const FileLocation &location(_doc->location(true));
			if (location.accessEnable()) {
				if (_doc->isAnimation() || _doc->isVideo() || QImageReader(location.name()).canRead()) {
					displayDocument(_doc, App::histItemById(_msgmigrated ? 0 : _channel, _msgid));
				}
				location.accessDisable();
			}
		}
	}
}

void MediaView::zoomIn() {
	int32 newZoom = _zoom;
	if (newZoom == ZoomToScreenLevel) {
		if (qCeil(_zoomToScreen) <= MaxZoomLevel) {
			newZoom = qCeil(_zoomToScreen);
		}
	} else {
		if (newZoom < _zoomToScreen && (newZoom + 1 > _zoomToScreen || (_zoomToScreen > MaxZoomLevel && newZoom == MaxZoomLevel))) {
			newZoom = ZoomToScreenLevel;
		} else if (newZoom < MaxZoomLevel) {
			++newZoom;
		}
	}
	zoomUpdate(newZoom);
}

void MediaView::zoomOut() {
	int32 newZoom = _zoom;
	if (newZoom == ZoomToScreenLevel) {
		if (qFloor(_zoomToScreen) >= -MaxZoomLevel) {
			newZoom = qFloor(_zoomToScreen);
		}
	} else {
		if (newZoom > _zoomToScreen && (newZoom - 1 < _zoomToScreen || (_zoomToScreen < -MaxZoomLevel && newZoom == -MaxZoomLevel))) {
			newZoom = ZoomToScreenLevel;
		} else if (newZoom > -MaxZoomLevel) {
			--newZoom;
		}
	}
	zoomUpdate(newZoom);
}

void MediaView::zoomReset() {
	int32 newZoom = _zoom;
	if (_zoom == 0) {
		if (qFloor(_zoomToScreen) == qCeil(_zoomToScreen) && qRound(_zoomToScreen) >= -MaxZoomLevel && qRound(_zoomToScreen) <= MaxZoomLevel) {
			newZoom = qRound(_zoomToScreen);
		} else {
			newZoom = ZoomToScreenLevel;
		}
	} else {
		newZoom = 0;
	}
	_x = -_width / 2;
	_y = -((gifShown() ? _gif->height() : (_current.height() / cIntRetinaFactor())) / 2);
	float64 z = (_zoom == ZoomToScreenLevel) ? _zoomToScreen : _zoom;
	if (z >= 0) {
		_x = qRound(_x * (z + 1));
		_y = qRound(_y * (z + 1));
	} else {
		_x = qRound(_x / (-z + 1));
		_y = qRound(_y / (-z + 1));
	}
	_x += width() / 2;
	_y += height() / 2;
	update();
	zoomUpdate(newZoom);
}

void MediaView::zoomUpdate(int32 &newZoom) {
	if (newZoom != ZoomToScreenLevel) {
		while ((newZoom < 0 && (-newZoom + 1) > _w) || (-newZoom + 1) > _h) {
			++newZoom;
		}
	}
	setZoomLevel(newZoom);
}

void MediaView::clearData() {
	if (!isHidden()) {
		hide();
	}
	if (!_animations.isEmpty()) {
		_animations.clear();
		_a_state.stop();
	}
	if (!_animOpacities.isEmpty()) _animOpacities.clear();
	stopGif();
	delete _menu;
	_menu = nullptr;
	_history = _migrated = nullptr;
	_peer = _from = nullptr;
	_user = nullptr;
	_photo = _additionalChatPhoto = nullptr;
	_doc = nullptr;
	_fullScreenVideo = false;
	_saveMsgText.clear();
	_caption.clear();
}

MediaView::~MediaView() {
	delete base::take(_menu);
}

void MediaView::clickHandlerActiveChanged(const ClickHandlerPtr &p, bool active) {
	setCursor((active || ClickHandler::getPressed()) ? style::cur_pointer : style::cur_default);
	update(QRegion(_saveMsg) + _captionRect);
}

void MediaView::clickHandlerPressedChanged(const ClickHandlerPtr &p, bool pressed) {
	setCursor((pressed || ClickHandler::getActive()) ? style::cur_pointer : style::cur_default);
	update(QRegion(_saveMsg) + _captionRect);
}

void MediaView::showSaveMsgFile() {
	psShowInFolder(_saveMsgFilename);
}

void MediaView::close() {
	if (_menu) _menu->hideMenu(true);
	if (App::wnd()) {
		Ui::hideLayer(true);
	}
}

void MediaView::activateControls() {
	if (!_menu && !_mousePressed) {
		_controlsHideTimer.start(int(st::mvWaitHide));
	}
	if (_fullScreenVideo) {
		if (_clipController) {
			_clipController->showAnimated();
		}
	}
	if (_controlsState == ControlsHiding || _controlsState == ControlsHidden) {
		_controlsState = ControlsShowing;
		_controlsAnimStarted = getms();
		a_cOpacity.start(1);
		if (!_a_state.animating()) _a_state.start();
	}
}

void MediaView::onHideControls(bool force) {
	if (!force) {
		if (!_dropdown.isHidden()
			|| _menu
			|| _mousePressed
			|| (_fullScreenVideo && _clipController && _clipController->geometry().contains(_lastMouseMovePos))) {
			return;
		}
	}
	if (_fullScreenVideo) {
		if (_clipController) {
			_clipController->hideAnimated();
		}
	}
	if (_controlsState == ControlsHiding || _controlsState == ControlsHidden) return;

	_controlsState = ControlsHiding;
	_controlsAnimStarted = getms();
	a_cOpacity.start(0);
	if (!_a_state.animating()) _a_state.start();
}

void MediaView::onDropdownHiding() {
	setFocus();
	_ignoringDropdown = true;
	_lastMouseMovePos = mapFromGlobal(QCursor::pos());
	updateOver(_lastMouseMovePos);
	_ignoringDropdown = false;
	if (!_controlsHideTimer.isActive()) {
		onHideControls(true);
	}
}

void MediaView::onScreenResized(int screen) {
	if (isHidden()) return;

	bool ignore = false;
	auto screens = QApplication::screens();
	if (screen >= 0 && screen < screens.size()) {
		if (auto screenHandle = windowHandle()->screen()) {
			if (screens.at(screen) != screenHandle) {
				ignore = true;
			}
		}
	}
	if (!ignore) {
		moveToScreen();
		auto item = (_msgid ? App::histItemById(_msgmigrated ? 0 : _channel, _msgid) : nullptr);
		if (_photo) {
			displayPhoto(_photo, item);
		} else if (_doc) {
			displayDocument(_doc, item);
		}
	}
}

void MediaView::onToMessage() {
	if (HistoryItem *item = _msgid ? App::histItemById(_msgmigrated ? 0 : _channel, _msgid) : 0) {
		if (App::wnd()) {
			close();
			Ui::showPeerHistoryAtItem(item);
		}
	}
}

void MediaView::onSaveAs() {
	QString file;
	if (_doc) {
		const FileLocation &location(_doc->location(true));
		if (!_doc->data().isEmpty() || location.accessEnable()) {
			QFileInfo alreadyInfo(location.name());
			QDir alreadyDir(alreadyInfo.dir());
			QString name = alreadyInfo.fileName(), filter;
			MimeType mimeType = mimeTypeForName(_doc->mime);
			QStringList p = mimeType.globPatterns();
			QString pattern = p.isEmpty() ? QString() : p.front();
			if (name.isEmpty()) {
				name = pattern.isEmpty() ? qsl(".unknown") : pattern.replace('*', QString());
			}

			if (pattern.isEmpty()) {
				filter = QString();
			} else {
				filter = mimeType.filterString() + qsl(";;") + filedialogAllFilesFilter();
			}

			psBringToBack(this);
			file = saveFileName(lang(lng_save_file), filter, qsl("doc"), name, true, alreadyDir);
			psShowOverAll(this);
			if (!file.isEmpty() && file != location.name()) {
				if (_doc->data().isEmpty()) {
					QFile(location.name()).copy(file);
				} else {
					QFile f(file);
					f.open(QIODevice::WriteOnly);
					f.write(_doc->data());
				}
			}

			if (_doc->data().isEmpty()) location.accessDisable();
		} else {
			if (!fileShown()) {
				DocumentSaveClickHandler::doSave(_doc, true);
				updateControls();
			} else {
				_saveVisible = false;
				update(_saveNav);
			}
			updateOver(_lastMouseMovePos);
		}
	} else {
		if (!_photo || !_photo->loaded()) return;

		psBringToBack(this);
		auto filter = qsl("JPEG Image (*.jpg);;") + filedialogAllFilesFilter();
		bool gotName = filedialogGetSaveFile(file, lang(lng_save_photo), filter, filedialogDefaultName(qsl("photo"), qsl(".jpg")));
		psShowOverAll(this);
		if (gotName) {
			if (!file.isEmpty()) {
				_photo->full->pix().toImage().save(file, "JPG");
			}
		}
	}
	activateWindow();
	Sandbox::setActiveWindow(this);
	setFocus();
}

void MediaView::onDocClick() {
	if (_doc->loading()) {
		onSaveCancel();
	} else {
		DocumentOpenClickHandler::doOpen(_doc, nullptr, ActionOnLoadNone);
		if (_doc->loading() && !_radial.animating()) {
			_radial.start(_doc->progress());
		}
	}
}

void MediaView::clipCallback(Media::Clip::Notification notification) {
	using namespace Media::Clip;

	if (!_gif) return;

	switch (notification) {
	case NotificationReinit: {
		if (auto item = App::histItemById(_msgmigrated ? 0 : _channel, _msgid)) {
			if (_gif->state() == State::Error) {
				stopGif();
				updateControls();
				update();
				break;
			} else if (_gif->state() == State::Finished) {
				_videoPositionMs = _videoDurationMs;
				_videoStopped = true;
				updateSilentVideoPlaybackState();
			} else {
				_videoIsSilent = _doc->isVideo() && !_gif->hasAudio();
				_videoDurationMs = _gif->getDurationMs();
				_videoPositionMs = _gif->getPositionMs();
				if (_videoIsSilent) {
					updateSilentVideoPlaybackState();
				}
			}
			displayDocument(_doc, item);
		} else {
			stopGif();
			updateControls();
			update();
		}
	} break;

	case NotificationRepaint: {
		if (!_gif->currentDisplayed()) {
			_videoPositionMs = _gif->getPositionMs();
			if (_videoIsSilent) {
				updateSilentVideoPlaybackState();
			}
			update(_x, _y, _w, _h);
		}
	} break;
	}
}

PeerData *MediaView::ui_getPeerForMouseAction() {
	return _history ? _history->peer : nullptr;
}

void MediaView::onDownload() {
	if (Global::AskDownloadPath()) {
		return onSaveAs();
	}

	QString path;
	if (Global::DownloadPath().isEmpty()) {
		path = psDownloadPath();
	} else if (Global::DownloadPath() == qsl("tmp")) {
		path = cTempDir();
	} else {
		path = Global::DownloadPath();
	}
	QString toName;
	if (_doc) {
		const FileLocation &location(_doc->location(true));
		if (location.accessEnable()) {
			if (!QDir().exists(path)) QDir().mkpath(path);
			toName = filedialogNextFilename(_doc->name, location.name(), path);
			if (toName != location.name() && !QFile(location.name()).copy(toName)) {
				toName = QString();
			}
			location.accessDisable();
		} else {
			if (!fileShown()) {
				DocumentSaveClickHandler::doSave(_doc);
				updateControls();
			} else {
				_saveVisible = false;
				update(_saveNav);
			}
			updateOver(_lastMouseMovePos);
		}
	} else {
		if (!_photo || !_photo->loaded()) {
			_saveVisible = false;
			update(_saveNav);
		} else {
			if (!QDir().exists(path)) QDir().mkpath(path);
			toName = filedialogDefaultName(qsl("photo"), qsl(".jpg"), path);
			if (!_photo->full->pix().toImage().save(toName, "JPG")) {
				toName = QString();
			}
		}
	}
	if (!toName.isEmpty()) {
		_saveMsgFilename = toName;
		_saveMsgStarted = getms();
		_saveMsgOpacity.start(1);
		updateImage();
	}
}

void MediaView::onSaveCancel() {
	if (_doc && _doc->loading()) {
		_doc->cancel();
	}
}

void MediaView::onShowInFolder() {
	if (!_doc) return;

	QString filepath = _doc->filepath(DocumentData::FilePathResolveChecked);
	if (!filepath.isEmpty()) {
		psShowInFolder(filepath);
	}
}

void MediaView::onForward() {
	HistoryItem *item = App::histItemById(_msgmigrated ? 0 : _channel, _msgid);
	if (!_msgid || !item) return;

	if (App::wnd()) {
		close();
		if (App::main()) {
			App::contextItem(item);
			App::main()->forwardLayer();
		}
	}
}

void MediaView::onDelete() {
	close();
	auto deletingPeerPhoto = [this]() {
		if (!_msgid) return true;
		if (_photo && _history) {
			auto lastPhoto = computeLastOverviewChatPhoto();
			if (lastPhoto.photo == _photo && _history->peer->photoId == _photo->id) {
				return _firstOpenedPeerPhoto;
			}
		}
		return false;
	};

	if (deletingPeerPhoto()) {
		App::main()->deletePhotoLayer(_photo);
	} else if (auto item = App::histItemById(_msgmigrated ? 0 : _channel, _msgid)) {
		App::contextItem(item);
		App::main()->deleteLayer();
	}
}

void MediaView::onOverview() {
	if (_menu) _menu->hideMenu(true);
	if (!_history || !typeHasMediaOverview(_overview)) {
		update();
		return;
	}
	close();
	if (_history->peer) App::main()->showMediaOverview(_history->peer, _overview);
}

void MediaView::onCopy() {
	if (!_dropdown.isHidden()) {
		_dropdown.ignoreShow();
		_dropdown.hideStart();
	}
	if (_doc) {
		if (!_current.isNull()) {
			QApplication::clipboard()->setPixmap(_current);
		} else if (gifShown()) {
			QApplication::clipboard()->setPixmap(_gif->frameOriginal());
		}
	} else {
		if (!_photo || !_photo->loaded()) return;

		QApplication::clipboard()->setPixmap(_photo->full->pix());
	}
}

void MediaView::showPhoto(PhotoData *photo, HistoryItem *context) {
	_history = context ? context->history() : nullptr;
	_migrated = nullptr;
	if (_history) {
		if (_history->peer->migrateFrom()) {
			_migrated = App::history(_history->peer->migrateFrom()->id);
		} else if (_history->peer->migrateTo()) {
			_migrated = _history;
			_history = App::history(_history->peer->migrateTo()->id);
		}
	}
	_additionalChatPhoto = nullptr;
	_firstOpenedPeerPhoto = false;
	_peer = 0;
	_user = 0;
	_saveMsgStarted = 0;
	_loadRequest = 0;
	_over = OverNone;
	_pressed = false;
	_dragging = 0;
	setCursor(style::cur_default);
	if (!_animations.isEmpty()) {
		_animations.clear();
		_a_state.stop();
	}
	if (!_animOpacities.isEmpty()) _animOpacities.clear();

	_index = -1;
	_msgid = context ? context->id : 0;
	_msgmigrated = context ? (context->history() == _migrated) : false;
	_channel = _history ? _history->channelId() : NoChannel;
	_canForward = _msgid > 0;
	_canDelete = context ? context->canDelete() : false;
	_photo = photo;
	if (_history) {
		if (context && !context->toHistoryMessage()) {
			_overview = OverviewChatPhotos;
			if (!_history->peer->isUser()) {
				computeAdditionalChatPhoto(_history->peer, computeLastOverviewChatPhoto().photo);
			}
		} else {
			_overview = OverviewPhotos;
		}
		findCurrent();
	}

	displayPhoto(photo, context);
	preloadData(0);
	activateControls();
}

void MediaView::showPhoto(PhotoData *photo, PeerData *context) {
	_history = _migrated = nullptr;
	_additionalChatPhoto = nullptr;
	_firstOpenedPeerPhoto = true;
	_peer = context;
	_user = context->asUser();
	_saveMsgStarted = 0;
	_loadRequest = 0;
	_over = OverNone;
	setCursor(style::cur_default);
	if (!_animations.isEmpty()) {
		_animations.clear();
		_a_state.stop();
	}
	if (!_animOpacities.isEmpty()) _animOpacities.clear();

	_msgid = 0;
	_msgmigrated = false;
	_channel = NoChannel;
	_canForward = _canDelete = false;
	_index = -1;
	_photo = photo;
	_overview = OverviewCount;
	if (_user) {
		if (_user->photos.isEmpty() && _user->photosCount < 0 && _user->photoId && _user->photoId != UnknownPeerPhotoId) {
			_index = 0;
		}
		for (int i = 0, l = _user->photos.size(); i < l; ++i) {
			if (_user->photos.at(i) == photo) {
				_index = i;
				break;
			}
		}

		if (_user->photosCount < 0) {
			loadBack();
		}
	} else if ((_history = App::historyLoaded(_peer))) {
		if (_history->peer->migrateFrom()) {
			_migrated = App::history(_history->peer->migrateFrom()->id);
		} else if (_history->peer->migrateTo()) {
			_migrated = _history;
			_history = App::history(_history->peer->migrateTo()->id);
		}

		auto lastChatPhoto = computeLastOverviewChatPhoto();
		if (_photo == lastChatPhoto.photo) {
			showPhoto(_photo, lastChatPhoto.item);
			_firstOpenedPeerPhoto = true;
			return;
		}

		computeAdditionalChatPhoto(_history->peer, lastChatPhoto.photo);
		if (_additionalChatPhoto == _photo) {
			_overview = OverviewChatPhotos;
			findCurrent();
		} else {
			_additionalChatPhoto = nullptr;
			_history = _migrated = nullptr;
		}
	}
	displayPhoto(photo, 0);
	preloadData(0);
	activateControls();
}

void MediaView::showDocument(DocumentData *doc, HistoryItem *context) {
	_photo = 0;
	_history = context ? context->history() : nullptr;
	_migrated = nullptr;
	if (_history) {
		if (_history->peer->migrateFrom()) {
			_migrated = App::history(_history->peer->migrateFrom()->id);
		} else if (_history->peer->migrateTo()) {
			_migrated = _history;
			_history = App::history(_history->peer->migrateTo()->id);
		}
	}
	_additionalChatPhoto = nullptr;
	_saveMsgStarted = 0;
	_peer = 0;
	_user = 0;
	_loadRequest = 0;
	_down = OverNone;
	_pressed = false;
	_dragging = 0;
	setCursor(style::cur_default);
	if (!_animations.isEmpty()) {
		_animations.clear();
		_a_state.stop();
	}
	if (!_animOpacities.isEmpty()) _animOpacities.clear();

	_index = -1;
	_msgid = context ? context->id : 0;
	_msgmigrated = context ? (context->history() == _migrated) : false;
	_channel = _history ? _history->channelId() : NoChannel;
	_canForward = _msgid > 0;
	_canDelete = context ? context->canDelete() : false;
	if (_history) {
		_overview = doc->isVideo() ? OverviewVideos : OverviewFiles;
		findCurrent();
	}
	if (doc->isVideo()) {
		_autoplayVideoDocument = doc;
	}
	displayDocument(doc, context);
	preloadData(0);
	activateControls();
}

void MediaView::displayPhoto(PhotoData *photo, HistoryItem *item) {
	stopGif();
	_doc = nullptr;
	_fullScreenVideo = false;
	_photo = photo;
	_radial.stop();

	_photoRadialRect = QRect(QPoint((width() - st::radialSize.width()) / 2, (height() - st::radialSize.height()) / 2), st::radialSize);

	_zoom = 0;

	_caption = Text();
	if (HistoryMessage *itemMsg = item ? item->toHistoryMessage() : nullptr) {
		if (HistoryPhoto *photoMsg = dynamic_cast<HistoryPhoto*>(itemMsg->getMedia())) {
			_caption.setMarkedText(st::mvCaptionFont, photoMsg->getCaption(), (item->author()->isUser() && item->author()->asUser()->botInfo) ? _captionBotOptions : _captionTextOptions);
		}
	}

	_zoomToScreen = 0;
	MTP::clearLoaderPriorities();
	_full = -1;
	_current = QPixmap();
	_down = OverNone;
	_w = convertScale(photo->full->width());
	_h = convertScale(photo->full->height());
	if (isHidden()) {
		moveToScreen();
	}
	if (_w > width()) {
		_h = qRound(_h * width() / float64(_w));
		_w = width();
	}
	if (_h > height()) {
		_w = qRound(_w * height() / float64(_h));
		_h = height();
	}
	_x = (width() - _w) / 2;
	_y = (height() - _h) / 2;
	_width = _w;
	if (_msgid && item) {
		_from = item->authorOriginal();
	} else {
		_from = _user;
	}
	_photo->download();
	displayFinished();
}

void MediaView::displayDocument(DocumentData *doc, HistoryItem *item) { // empty messages shown as docs: doc can be NULL
	if (!doc || (!doc->isAnimation() && !doc->isVideo()) || doc != _doc || (item && (item->id != _msgid || (item->history() != (_msgmigrated ? _migrated : _history))))) {
		_fullScreenVideo = false;
		_current = QPixmap();
		stopGif();
	} else if (gifShown()) {
		_current = QPixmap();
	}
	_doc = doc;
	_photo = nullptr;
	_radial.stop();

	if (_autoplayVideoDocument && _doc != _autoplayVideoDocument) {
		_autoplayVideoDocument = nullptr;
	}

	_caption = Text();
	if (_doc) {
		if (_doc->sticker()) {
			_doc->checkSticker();
			if (!_doc->sticker()->img->isNull()) {
				_current = _doc->sticker()->img->pix();
			} else {
				_current = _doc->thumb->pixBlurred(_doc->dimensions.width(), _doc->dimensions.height());
			}
		} else {
			_doc->automaticLoad(item);

			if (_doc->isAnimation() || _doc->isVideo()) {
				initAnimation();
			} else {
				const FileLocation &location(_doc->location(true));
				if (location.accessEnable()) {
					if (QImageReader(location.name()).canRead()) {
						_current = App::pixmapFromImageInPlace(App::readImage(location.name(), 0, false));
					}
				}
				location.accessDisable();
			}
		}
	}

	_docIconRect = QRect((width() - st::mvDocIconSize) / 2, (height() - st::mvDocIconSize) / 2, st::mvDocIconSize, st::mvDocIconSize);
	if (!fileShown()) {
		if (!_doc || _doc->thumb->isNull()) {
			int32 colorIndex = documentColorIndex(_doc, _docExt);
			_docIconColor = documentColor(colorIndex);
			const style::icon *(thumbs[]) = { &st::mediaviewFileBlue, &st::mediaviewFileGreen, &st::mediaviewFileRed, &st::mediaviewFileYellow };
			_docIcon = thumbs[colorIndex];

			int32 extmaxw = (st::mvDocIconSize - st::mvDocExtPadding * 2);
			_docExtWidth = st::mvDocExtFont->width(_docExt);
			if (_docExtWidth > extmaxw) {
				_docExt = st::mvDocNameFont->elided(_docExt, extmaxw, Qt::ElideMiddle);
				_docExtWidth = st::mvDocNameFont->width(_docExt);
			}
		} else {
			_doc->thumb->load();
			int32 tw = _doc->thumb->width(), th = _doc->thumb->height();
			if (!tw || !th) {
				_docThumbx = _docThumby = _docThumbw = 0;
			} else if (tw > th) {
				_docThumbw = (tw * st::mvDocIconSize) / th;
				_docThumbx = (_docThumbw - st::mvDocIconSize) / 2;
				_docThumby = 0;
			} else {
				_docThumbw = st::mvDocIconSize;
				_docThumbx = 0;
				_docThumby = ((th * _docThumbw) / tw - st::mvDocIconSize) / 2;
			}
		}

		int32 maxw = st::mvDocSize.width() - st::mvDocIconSize - st::mvDocPadding * 3;

		if (_doc) {
			_docName = (_doc->type == StickerDocument) ? lang(lng_in_dlg_sticker) : (_doc->type == AnimatedDocument ? qsl("GIF") : (_doc->name.isEmpty() ? lang(lng_mediaview_doc_image) : _doc->name));
		} else {
			_docName = lang(lng_message_empty);
		}
		_docNameWidth = st::mvDocNameFont->width(_docName);
		if (_docNameWidth > maxw) {
			_docName = st::mvDocNameFont->elided(_docName, maxw, Qt::ElideMiddle);
			_docNameWidth = st::mvDocNameFont->width(_docName);
		}

		// _docSize is updated in updateControls()

		_docRect = QRect((width() - st::mvDocSize.width()) / 2, (height() - st::mvDocSize.height()) / 2, st::mvDocSize.width(), st::mvDocSize.height());
		_docIconRect = myrtlrect(_docRect.x() + st::mvDocPadding, _docRect.y() + st::mvDocPadding, st::mvDocIconSize, st::mvDocIconSize);
	} else if (!_current.isNull()) {
		_current.setDevicePixelRatio(cRetinaFactor());
		_w = convertScale(_current.width());
		_h = convertScale(_current.height());
	} else {
		_w = convertScale(_gif->width());
		_h = convertScale(_gif->height());
	}
	if (isHidden()) {
		moveToScreen();
	}
	_width = _w;
	if (_w > 0 && _h > 0) {
		_zoomToScreen = float64(width()) / _w;
		if (_h * _zoomToScreen > height()) {
			_zoomToScreen = float64(height()) / _h;
		}
		if (_zoomToScreen >= 1.) {
			_zoomToScreen -= 1.;
		} else {
			_zoomToScreen = 1. - (1. / _zoomToScreen);
		}
	} else {
		_zoomToScreen = 0;
	}
	if ((_w > width()) || (_h > height()) || _fullScreenVideo) {
		_zoom = ZoomToScreenLevel;
		if (_zoomToScreen >= 0) {
			_w = qRound(_w * (_zoomToScreen + 1));
			_h = qRound(_h * (_zoomToScreen + 1));
		} else {
			_w = qRound(_w / (-_zoomToScreen + 1));
			_h = qRound(_h / (-_zoomToScreen + 1));
		}
		snapXY();
	} else {
		_zoom = 0;
	}
	_x = (width() - _w) / 2;
	_y = (height() - _h) / 2;
	if (_msgid && item) {
		_from = item->authorOriginal();
	} else {
		_from = _user;
	}
	_full = 1;
	displayFinished();
}

void MediaView::displayFinished() {
	updateControls();
	if (isHidden()) {
		psUpdateOverlayed(this);
		show();
		psShowOverAll(this);
		activateWindow();
		Sandbox::setActiveWindow(this);
		setFocus();
	}
}

void MediaView::initAnimation() {
	t_assert(_doc != nullptr);
	t_assert(_doc->isAnimation() || _doc->isVideo());

	auto &location = _doc->location(true);
	if (!_doc->data().isEmpty()) {
		createClipReader();
	} else if (location.accessEnable()) {
		createClipReader();
		location.accessDisable();
	} else if (_doc->dimensions.width() && _doc->dimensions.height()) {
		int w = _doc->dimensions.width();
		int h = _doc->dimensions.height();
		_current = _doc->thumb->pixNoCache(w, h, ImagePixSmooth | ImagePixBlurred, w / cIntRetinaFactor(), h / cIntRetinaFactor());
		if (cRetina()) _current.setDevicePixelRatio(cRetinaFactor());
	} else {
		_current = _doc->thumb->pixNoCache(_doc->thumb->width(), _doc->thumb->height(), ImagePixSmooth | ImagePixBlurred, st::mvDocIconSize, st::mvDocIconSize);
	}
}

void MediaView::createClipReader() {
	if (_gif) return;

	t_assert(_doc != nullptr);
	t_assert(_doc->isAnimation() || _doc->isVideo());

	if (_doc->dimensions.width() && _doc->dimensions.height()) {
		int w = _doc->dimensions.width();
		int h = _doc->dimensions.height();
		_current = _doc->thumb->pixNoCache(w, h, ImagePixSmooth | ImagePixBlurred, w / cIntRetinaFactor(), h / cIntRetinaFactor());
		if (cRetina()) _current.setDevicePixelRatio(cRetinaFactor());
	} else {
		_current = _doc->thumb->pixNoCache(_doc->thumb->width(), _doc->thumb->height(), ImagePixSmooth | ImagePixBlurred, st::mvDocIconSize, st::mvDocIconSize);
	}
	auto mode = _doc->isVideo() ? Media::Clip::Reader::Mode::Video : Media::Clip::Reader::Mode::Gif;
	_gif = std_::make_unique<Media::Clip::Reader>(_doc->location(), _doc->data(), [this](Media::Clip::Notification notification) {
		clipCallback(notification);
	}, mode);

	// Correct values will be set when gif gets inited.
	_videoPaused = _videoIsSilent = _videoStopped = false;
	_videoPositionMs = 0ULL;
	_videoDurationMs = _doc->duration() * 1000ULL;

	createClipController();
}

void MediaView::createClipController() {
	if (!_doc->isVideo()) return;

	_clipController.destroy();
	_clipController = new Media::Clip::Controller(this);
	setClipControllerGeometry();
	_clipController->show();

	connect(_clipController, SIGNAL(playPressed()), this, SLOT(onVideoPauseResume()));
	connect(_clipController, SIGNAL(pausePressed()), this, SLOT(onVideoPauseResume()));
	connect(_clipController, SIGNAL(seekProgress(int64)), this, SLOT(onVideoSeekProgress(int64)));
	connect(_clipController, SIGNAL(seekFinished(int64)), this, SLOT(onVideoSeekFinished(int64)));
	connect(_clipController, SIGNAL(volumeChanged(float64)), this, SLOT(onVideoVolumeChanged(float64)));
	connect(_clipController, SIGNAL(toFullScreenPressed()), this, SLOT(onVideoToggleFullScreen()));
	connect(_clipController, SIGNAL(fromFullScreenPressed()), this, SLOT(onVideoToggleFullScreen()));

	if (audioPlayer()) {
		connect(audioPlayer(), SIGNAL(updated(const AudioMsgId&)), this, SLOT(onVideoPlayProgress(const AudioMsgId&)));
	}
}

void MediaView::setClipControllerGeometry() {
	t_assert(_clipController != nullptr);

	int controllerBottom = _captionRect.isEmpty() ? height() : _captionRect.y();
	_clipController->setGeometry(
		(width() - _clipController->width()) / 2,
		controllerBottom - _clipController->height() - st::mvCaptionPadding.bottom() - st::mvCaptionMargin.height(),
		st::mediaviewControllerSize.width(),
		st::mediaviewControllerSize.height());
	myEnsureResized(_clipController);
}

void MediaView::onVideoPauseResume() {
	if (!_gif) return;

	if (auto item = App::histItemById(_msgmigrated ? 0 : _channel, _msgid)) {
		if (_gif->state() == Media::Clip::State::Error) {
			displayDocument(_doc, item);
		} else if (_gif->state() == Media::Clip::State::Finished) {
			restartVideoAtSeekPosition(0);
		} else {
			_gif->pauseResumeVideo();
			_videoPaused = _gif->videoPaused();
			if (_videoIsSilent) {
				updateSilentVideoPlaybackState();
			}
		}
	} else {
		stopGif();
		updateControls();
		update();
	}
}

void MediaView::restartVideoAtSeekPosition(int64 positionMs) {
	_autoplayVideoDocument = _doc;

	if (_current.isNull()) {
		_current = _gif->current(_gif->width(), _gif->height(), _gif->width(), _gif->height(), getms());
	}
	_gif = std_::make_unique<Media::Clip::Reader>(_doc->location(), _doc->data(), [this](Media::Clip::Notification notification) {
		clipCallback(notification);
	}, Media::Clip::Reader::Mode::Video, positionMs);

	// Correct values will be set when gif gets inited.
	_videoPaused = _videoIsSilent = _videoStopped = false;
	_videoPositionMs = positionMs;

	AudioPlaybackState state;
	state.state = AudioPlayerPlaying;
	state.position = _videoPositionMs;
	state.duration = _videoDurationMs;
	state.frequency = _videoFrequencyMs;
	updateVideoPlaybackState(state);
}

void MediaView::onVideoSeekProgress(int64 positionMs) {
	if (!_videoPaused && !_videoStopped) {
		onVideoPauseResume();
	}
}

void MediaView::onVideoSeekFinished(int64 positionMs) {
	restartVideoAtSeekPosition(positionMs);
}

void MediaView::onVideoVolumeChanged(float64 volume) {
	Global::SetVideoVolume(volume);
	Global::RefVideoVolumeChanged().notify();
}

void MediaView::onVideoToggleFullScreen() {
	if (!_clipController) return;

	_fullScreenVideo = !_fullScreenVideo;
	if (_fullScreenVideo) {
		_fullScreenZoomCache = _zoom;
		setZoomLevel(ZoomToScreenLevel);
	} else {
		setZoomLevel(_fullScreenZoomCache);
		_clipController->showAnimated();
	}

	_clipController->setInFullScreen(_fullScreenVideo);
	updateControls();
	update();
}

void MediaView::onVideoPlayProgress(const AudioMsgId &audioId) {
	if (audioId.type() != AudioMsgId::Type::Video || !_gif) {
		return;
	}

	t_assert(audioPlayer() != nullptr);
	auto state = audioPlayer()->currentVideoState(_gif->playId());
	if (state.duration) {
		updateVideoPlaybackState(state);
	}
}

void MediaView::updateVideoPlaybackState(const AudioPlaybackState &state) {
	if (state.frequency) {
		if (state.state & AudioPlayerStoppedMask) {
			_videoStopped = true;
		}
		_clipController->updatePlayback(state);
	} else { // Audio has stopped already.
		_videoIsSilent = true;
		updateSilentVideoPlaybackState();
	}
}

void MediaView::updateSilentVideoPlaybackState() {
	AudioPlaybackState state;
	if (_videoPaused) {
		state.state = AudioPlayerPaused;
	} else if (_videoPositionMs == _videoDurationMs) {
		state.state = AudioPlayerStoppedAtEnd;
	} else {
		state.state = AudioPlayerPlaying;
	}
	state.position = _videoPositionMs;
	state.duration = _videoDurationMs;
	state.frequency = _videoFrequencyMs;
	updateVideoPlaybackState(state);
}

void MediaView::paintEvent(QPaintEvent *e) {
	QRect r(e->rect());
	QRegion region(e->region());
	QVector<QRect> rs(region.rects());

	uint64 ms = getms();

	Painter p(this);

	bool name = false;

	p.setClipRegion(region);

	// main bg
	QPainter::CompositionMode m = p.compositionMode();
	p.setCompositionMode(QPainter::CompositionMode_Source);
	if (_fullScreenVideo) {
		for (int i = 0, l = region.rectCount(); i < l; ++i) {
			p.fillRect(rs.at(i), st::black);
		}
	} else {
		p.setOpacity(st::mvBgOpacity);
		for (int i = 0, l = region.rectCount(); i < l; ++i) {
			p.fillRect(rs.at(i), st::mvBgColor);
		}
		p.setCompositionMode(m);
	}

	// photo
	if (_photo) {
		int32 w = _width * cIntRetinaFactor();
		if (_full <= 0 && _photo->loaded()) {
			int32 h = int((_photo->full->height() * (qreal(w) / qreal(_photo->full->width()))) + 0.9999);
			_current = _photo->full->pixNoCache(w, h, ImagePixSmooth);
			if (cRetina()) _current.setDevicePixelRatio(cRetinaFactor());
			_full = 1;
		} else if (_full < 0 && _photo->medium->loaded()) {
			int32 h = int((_photo->full->height() * (qreal(w) / qreal(_photo->full->width()))) + 0.9999);
			_current = _photo->medium->pixNoCache(w, h, ImagePixSmooth | ImagePixBlurred);
			if (cRetina()) _current.setDevicePixelRatio(cRetinaFactor());
			_full = 0;
		} else if (_current.isNull() && _photo->thumb->loaded()) {
			int32 h = int((_photo->full->height() * (qreal(w) / qreal(_photo->full->width()))) + 0.9999);
			_current = _photo->thumb->pixNoCache(w, h, ImagePixSmooth | ImagePixBlurred);
			if (cRetina()) _current.setDevicePixelRatio(cRetinaFactor());
		} else if (_current.isNull()) {
			_current = _photo->thumb->pix();
		}
	}
	p.setOpacity(1);
	if (_photo || fileShown()) {
		QRect imgRect(_x, _y, _w, _h);
		if (imgRect.intersects(r)) {
			QPixmap toDraw = _current.isNull() ? _gif->current(_gif->width(), _gif->height(), _gif->width(), _gif->height(), ms) : _current;
			if (!_gif && (!_doc || !_doc->sticker() || _doc->sticker()->img->isNull()) && toDraw.hasAlpha()) {
				p.fillRect(imgRect, _transparentBrush);
			}
			if (toDraw.width() != _w * cIntRetinaFactor()) {
				bool was = (p.renderHints() & QPainter::SmoothPixmapTransform);
				if (!was) p.setRenderHint(QPainter::SmoothPixmapTransform, true);
				p.drawPixmap(QRect(_x, _y, _w, _h), toDraw);
				if (!was) p.setRenderHint(QPainter::SmoothPixmapTransform, false);
			} else {
				p.drawPixmap(_x, _y, toDraw);
			}

			bool radial = false;
			float64 radialOpacity = 0;
			if (_radial.animating()) {
				_radial.step(ms);
				radial = _radial.animating();
				radialOpacity = _radial.opacity();
			}
			if (_photo) {
				if (radial) {
					auto inner = radialRect();

					p.setPen(Qt::NoPen);
					p.setBrush(st::black);
					p.setOpacity(radialOpacity * st::radialBgOpacity);

					p.setRenderHint(QPainter::HighQualityAntialiasing);
					p.drawEllipse(inner);
					p.setRenderHint(QPainter::HighQualityAntialiasing, false);

					p.setOpacity(1);
					QRect arc(inner.marginsRemoved(QMargins(st::radialLine, st::radialLine, st::radialLine, st::radialLine)));
					_radial.draw(p, arc, st::radialLine, st::white);
				}
			} else if (_doc) {
				paintDocRadialLoading(p, radial, radialOpacity);
			}

			if (_saveMsgStarted) {
				auto ms = getms();
				float64 dt = float64(ms) - _saveMsgStarted, hidingDt = dt - st::medviewSaveMsgShowing - st::medviewSaveMsgShown;
				if (dt < st::medviewSaveMsgShowing + st::medviewSaveMsgShown + st::medviewSaveMsgHiding) {
					if (hidingDt >= 0 && _saveMsgOpacity.to() > 0.5) {
						_saveMsgOpacity.start(0);
					}
					float64 progress = (hidingDt >= 0) ? (hidingDt / st::medviewSaveMsgHiding) : (dt / st::medviewSaveMsgShowing);
					_saveMsgOpacity.update(qMin(progress, 1.), anim::linear);
                    if (_saveMsgOpacity.current() > 0) {
						p.setOpacity(_saveMsgOpacity.current());
						App::roundRect(p, _saveMsg, st::medviewSaveMsg, MediaviewSaveCorners);
						st::medviewSaveMsgCheck.paint(p, _saveMsg.topLeft() + st::medviewSaveMsgCheckPos, width());

						p.setPen(st::white->p);
						textstyleSet(&st::medviewSaveAsTextStyle);
						_saveMsgText.draw(p, _saveMsg.x() + st::medviewSaveMsgPadding.left(), _saveMsg.y() + st::medviewSaveMsgPadding.top(), _saveMsg.width() - st::medviewSaveMsgPadding.left() - st::medviewSaveMsgPadding.right());
						textstyleRestore();
						p.setOpacity(1);
					}
					if (_full >= 1) {
                        uint64 nextFrame = (dt < st::medviewSaveMsgShowing || hidingDt >= 0) ? int(AnimationTimerDelta) : (st::medviewSaveMsgShowing + st::medviewSaveMsgShown + 1 - dt);
						_saveMsgUpdater.start(nextFrame);
					}
				} else {
					_saveMsgStarted = 0;
				}
			}
		}
	} else {
		if (_docRect.intersects(r)) {
			p.fillRect(_docRect, st::mvDocBg->b);
			if (_docIconRect.intersects(r)) {
				bool radial = false;
				float64 radialOpacity = 0;
				if (_radial.animating()) {
					_radial.step(ms);
					radial = _radial.animating();
					radialOpacity = _radial.opacity();
				}
				if (!_doc || _doc->thumb->isNull()) {
					p.fillRect(_docIconRect, _docIconColor->b);
					if ((!_doc || _doc->loaded()) && (!radial || radialOpacity < 1) && _docIcon) {
						_docIcon->paint(p, _docIconRect.x() + (_docIconRect.width() - _docIcon->width()), _docIconRect.y(), width());
						p.setPen(st::mvDocExtColor->p);
						p.setFont(st::mvDocExtFont->f);
						if (!_docExt.isEmpty()) {
							p.drawText(_docIconRect.x() + (_docIconRect.width() - _docExtWidth) / 2, _docIconRect.y() + st::mvDocExtTop + st::mvDocExtFont->ascent, _docExt);
						}
					}
				} else {
					int32 rf(cIntRetinaFactor());
					p.drawPixmap(_docIconRect.topLeft(), _doc->thumb->pix(_docThumbw), QRect(_docThumbx * rf, _docThumby * rf, st::mvDocIconSize * rf, st::mvDocIconSize * rf));
				}

				paintDocRadialLoading(p, radial, radialOpacity);
			}

			if (!_docIconRect.contains(r)) {
				name = true;
				p.setPen(st::mvDocNameColor);
				p.setFont(st::mvDocNameFont);
				p.drawTextLeft(_docRect.x() + 2 * st::mvDocPadding + st::mvDocIconSize, _docRect.y() + st::mvDocPadding + st::mvDocNameTop, width(), _docName, _docNameWidth);

				p.setPen(st::mvDocSizeColor);
				p.setFont(st::mvFont);
				p.drawTextLeft(_docRect.x() + 2 * st::mvDocPadding + st::mvDocIconSize, _docRect.y() + st::mvDocPadding + st::mvDocSizeTop, width(), _docSize, _docSizeWidth);
			}
		}
	}

	float64 co = _fullScreenVideo ? 0. : a_cOpacity.current();
	if (co > 0) {
		// left nav bar
		if (_leftNav.intersects(r) && _leftNavVisible) {
			auto o = overLevel(OverLeftNav);
			if (o > 0) {
				p.setOpacity(o * st::mvControlBgOpacity * co);
				for (int i = 0, l = region.rectCount(); i < l; ++i) {
					auto fill = _leftNav.intersected(rs.at(i));
					if (!fill.isEmpty()) p.fillRect(fill, st::black->b);
				}
			}
			if (_leftNavIcon.intersects(r)) {
				p.setOpacity((o * st::mvIconOverOpacity + (1 - o) * st::mvIconOpacity) * co);
				st::mediaviewLeft.paintInCenter(p, _leftNavIcon);
			}
		}

		// right nav bar
		if (_rightNav.intersects(r) && _rightNavVisible) {
			auto o = overLevel(OverRightNav);
			if (o > 0) {
				p.setOpacity(o * st::mvControlBgOpacity * co);
				for (int i = 0, l = region.rectCount(); i < l; ++i) {
					auto fill = _rightNav.intersected(rs.at(i));
					if (!fill.isEmpty()) p.fillRect(fill, st::black);
				}
			}
			if (_rightNavIcon.intersects(r)) {
				p.setOpacity((o * st::mvIconOverOpacity + (1 - o) * st::mvIconOpacity) * co);
				st::mediaviewRight.paintInCenter(p, _rightNavIcon);
			}
		}

		// close button
		if (_closeNav.intersects(r)) {
			auto o = overLevel(OverClose);
			if (o > 0) {
				p.setOpacity(o * st::mvControlBgOpacity * co);
				for (int i = 0, l = region.rectCount(); i < l; ++i) {
					auto fill = _closeNav.intersected(rs.at(i));
					if (!fill.isEmpty()) p.fillRect(fill, st::black);
				}
			}
			if (_closeNavIcon.intersects(r)) {
				p.setOpacity((o * st::mvIconOverOpacity + (1 - o) * st::mvIconOpacity) * co);
				st::mediaviewClose.paintInCenter(p, _closeNavIcon);
			}
		}

		// save button
		if (_saveVisible && _saveNavIcon.intersects(r)) {
			auto o = overLevel(OverSave);
			p.setOpacity((o * st::mvIconOverOpacity + (1 - o) * st::mvIconOpacity) * co);
			st::mediaviewSave.paintInCenter(p, _saveNavIcon);
		}

		// more area
		if (_moreNavIcon.intersects(r)) {
			auto o = overLevel(OverMore);
			p.setOpacity((o * st::mvIconOverOpacity + (1 - o) * st::mvIconOpacity) * co);
			st::mediaviewMore.paintInCenter(p, _moreNavIcon);
		}

		p.setPen(st::white);
		p.setFont(st::mvThickFont);

		// header
		if (_headerNav.intersects(r)) {
			float64 o = _headerHasLink ? overLevel(OverHeader) : 0;
			p.setOpacity((o * st::mvIconOverOpacity + (1 - o) * st::mvIconOpacity) * co);
			p.drawText(_headerNav.left(), _headerNav.top() + st::mvThickFont->ascent, _headerText);

			if (o > 0) {
				p.setOpacity(o * co);
				p.drawLine(_headerNav.left(), _headerNav.top() + st::mvThickFont->ascent + 1, _headerNav.right(), _headerNav.top() + st::mvThickFont->ascent + 1);
			}
		}

		p.setFont(st::mvFont->f);

		// name
		if (_from && _nameNav.intersects(r)) {
			float64 o = overLevel(OverName);
			p.setOpacity((o * st::mvIconOverOpacity + (1 - o) * st::mvIconOpacity) * co);
			_fromName.drawElided(p, _nameNav.left(), _nameNav.top(), _nameNav.width());

			if (o > 0) {
				p.setOpacity(o * co);
				p.drawLine(_nameNav.left(), _nameNav.top() + st::mvFont->ascent + 1, _nameNav.right(), _nameNav.top() + st::mvFont->ascent + 1);
			}
		}

		// date
		if (_dateNav.intersects(r)) {
			float64 o = overLevel(OverDate);
			p.setOpacity((o * st::mvIconOverOpacity + (1 - o) * st::mvIconOpacity) * co);
			p.drawText(_dateNav.left(), _dateNav.top() + st::mvFont->ascent, _dateText);

			if (o > 0) {
				p.setOpacity(o * co);
				p.drawLine(_dateNav.left(), _dateNav.top() + st::mvFont->ascent + 1, _dateNav.right(), _dateNav.top() + st::mvFont->ascent + 1);
			}
		}

		// caption
		if (!_caption.isEmpty()) {
			QRect outer(_captionRect.marginsAdded(st::mvCaptionPadding));
			if (outer.intersects(r)) {
				p.setOpacity(co);
				p.setBrush(st::mvCaptionBg->b);
				p.setPen(Qt::NoPen);
				p.drawRoundedRect(outer, st::mvCaptionRadius, st::mvCaptionRadius);
				if (_captionRect.intersects(r)) {
					textstyleSet(&st::medviewSaveAsTextStyle);
					p.setPen(st::white->p);
					_caption.drawElided(p, _captionRect.x(), _captionRect.y(), _captionRect.width(), _captionRect.height() / st::mvCaptionFont->height);
					textstyleRestore();
				}
			}
		}
	}
}

void MediaView::paintDocRadialLoading(Painter &p, bool radial, float64 radialOpacity) {
	float64 o = overLevel(OverIcon);
	if (radial) {
		if (!_doc->loaded() && radialOpacity < 1) {
			p.setOpacity((o * 1. + (1 - o) * st::radialDownloadOpacity) * (1 - radialOpacity));
			p.drawSpriteCenter(_docIconRect, st::radialDownload);
		}

		QRect inner(QPoint(_docIconRect.x() + ((_docIconRect.width() - st::radialSize.width()) / 2), _docIconRect.y() + ((_docIconRect.height() - st::radialSize.height()) / 2)), st::radialSize);
		p.setPen(Qt::NoPen);
		p.setBrush(st::black);
		p.setOpacity(radialOpacity * st::radialBgOpacity);

		p.setRenderHint(QPainter::HighQualityAntialiasing);
		p.drawEllipse(inner);
		p.setRenderHint(QPainter::HighQualityAntialiasing, false);

		p.setOpacity((o * 1. + (1 - o) * st::radialCancelOpacity) * radialOpacity);
		p.drawSpriteCenter(_docIconRect, st::radialCancel);
		p.setOpacity(1);

		QRect arc(inner.marginsRemoved(QMargins(st::radialLine, st::radialLine, st::radialLine, st::radialLine)));
		_radial.draw(p, arc, st::radialLine, st::white);
	} else if (_doc && !_doc->loaded()) {
		p.setOpacity((o * 1. + (1 - o) * st::radialDownloadOpacity));
		p.drawSpriteCenter(_docIconRect, st::radialDownload);
	}
}

void MediaView::keyPressEvent(QKeyEvent *e) {
	if (_clipController) {
		auto toggle1 = (e->key() == Qt::Key_F && e->modifiers().testFlag(Qt::ControlModifier));
		auto toggle2 = (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) && (e->modifiers().testFlag(Qt::AltModifier) || e->modifiers().testFlag(Qt::ControlModifier));
		if (toggle1 || toggle2) {
			onVideoToggleFullScreen();
			return;
		}
		if (_fullScreenVideo) {
			if (e->key() == Qt::Key_Escape) {
				onVideoToggleFullScreen();
			} else if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return || e->key() == Qt::Key_Space) {
				onVideoPauseResume();
			}
			return;
		}
	}
	if (!_menu && e->key() == Qt::Key_Escape) {
		if (_doc && _doc->loading()) {
			onDocClick();
		} else {
			close();
		}
	} else if (e == QKeySequence::Save || e == QKeySequence::SaveAs) {
		onSaveAs();
	} else if (e->key() == Qt::Key_Copy || (e->key() == Qt::Key_C && e->modifiers().testFlag(Qt::ControlModifier))) {
		onCopy();
	} else if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return || e->key() == Qt::Key_Space) {
		if (_doc && !_doc->loading() && (!fileShown() || !_doc->loaded())) {
			onDocClick();
		} else if (_doc && _doc->isVideo()) {
			onVideoPauseResume();
		}
	} else if (e->key() == Qt::Key_Left) {
		moveToNext(-1);
	} else if (e->key() == Qt::Key_Right) {
		moveToNext(1);
	} else if (e->modifiers().testFlag(Qt::ControlModifier) && (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal || e->key() == ']' || e->key() == Qt::Key_Asterisk || e->key() == Qt::Key_Minus || e->key() == Qt::Key_Underscore || e->key() == Qt::Key_0)) {
		if (e->key() == Qt::Key_Plus || e->key() == Qt::Key_Equal || e->key() == Qt::Key_Asterisk || e->key() == ']') {
			zoomIn();
		} else if (e->key() == Qt::Key_Minus || e->key() == Qt::Key_Underscore) {
			zoomOut();
		} else {
			zoomReset();
		}
	}
}

void MediaView::wheelEvent(QWheelEvent *e) {
#ifdef OS_MAC_OLD
	constexpr auto step = 120;
#else // OS_MAC_OLD
	constexpr auto step = static_cast<int>(QWheelEvent::DefaultDeltasPerStep);
#endif // OS_MAC_OLD

	_verticalWheelDelta += e->angleDelta().y();
	while (qAbs(_verticalWheelDelta) >= step) {
		if (_verticalWheelDelta < 0) {
			_verticalWheelDelta += step;
			if (e->modifiers().testFlag(Qt::ControlModifier)) {
				zoomOut();
			} else {
#ifndef OS_MAC_OLD
				if (e->source() == Qt::MouseEventNotSynthesized) {
					moveToNext(1);
				}
#endif // OS_MAC_OLD
			}
		} else {
			_verticalWheelDelta -= step;
			if (e->modifiers().testFlag(Qt::ControlModifier)) {
				zoomIn();
			} else {
#ifndef OS_MAC_OLD
				if (e->source() == Qt::MouseEventNotSynthesized) {
					moveToNext(-1);
				}
#endif // OS_MAC_OLD
			}
		}
	}
}

void MediaView::setZoomLevel(int newZoom) {
	if (_zoom == newZoom) return;

	float64 nx, ny, z = (_zoom == ZoomToScreenLevel) ? _zoomToScreen : _zoom;
	_w = gifShown() ? convertScale(_gif->width()) : (convertScale(_current.width()) / cIntRetinaFactor());
	_h = gifShown() ? convertScale(_gif->height()) : (convertScale(_current.height()) / cIntRetinaFactor());
	if (z >= 0) {
		nx = (_x - width() / 2.) / (z + 1);
		ny = (_y - height() / 2.) / (z + 1);
	} else {
		nx = (_x - width() / 2.) * (-z + 1);
		ny = (_y - height() / 2.) * (-z + 1);
	}
	_zoom = newZoom;
	z = (_zoom == ZoomToScreenLevel) ? _zoomToScreen : _zoom;
	if (z > 0) {
		_w = qRound(_w * (z + 1));
		_h = qRound(_h * (z + 1));
		_x = qRound(nx * (z + 1) + width() / 2.);
		_y = qRound(ny * (z + 1) + height() / 2.);
	} else {
		_w = qRound(_w / (-z + 1));
		_h = qRound(_h / (-z + 1));
		_x = qRound(nx / (-z + 1) + width() / 2.);
		_y = qRound(ny / (-z + 1) + height() / 2.);
	}
	snapXY();
	update();
}

bool MediaView::moveToNext(int32 delta) {
	if (_index < 0) {
		if (delta == -1 && _photo == _additionalChatPhoto) {
			auto lastChatPhoto = computeLastOverviewChatPhoto();
			if (lastChatPhoto.item) {
				if (lastChatPhoto.item->history() == _history) {
					_index = _history->overview[_overview].size() - 1;
					_msgmigrated = false;
				} else {
					_index = _migrated->overview[_overview].size() - 1;
					_msgmigrated = true;
				}
				_msgid = lastChatPhoto.item->id;
				_channel = _history ? _history->channelId() : NoChannel;
				_canForward = _msgid > 0;
				_canDelete = lastChatPhoto.item->canDelete();
				stopGif();
				displayPhoto(lastChatPhoto.photo, lastChatPhoto.item); preloadData(delta);
				return true;
			} else if (_history && (_history->overviewCount(OverviewChatPhotos) != 0 || (
				_migrated && _migrated->overviewCount(OverviewChatPhotos) != 0))) {
				loadBack();
				return true;
			}
		}
		return false;
	}
	if ((_history && _overview != OverviewPhotos && _overview != OverviewChatPhotos && _overview != OverviewFiles && _overview != OverviewVideos) || (_overview == OverviewCount && !_user)) {
		return false;
	}
	if (_msgmigrated && !_history->overviewLoaded(_overview)) {
		return true;
	}

	int32 newIndex = _index + delta;
	if (_history && _overview != OverviewCount) {
		bool newMigrated = _msgmigrated;
		if (!newMigrated && newIndex < 0 && _migrated) {
			newIndex += _migrated->overview[_overview].size();
			newMigrated = true;
		} else if (newMigrated && newIndex >= _migrated->overview[_overview].size()) {
			newIndex -= _migrated->overview[_overview].size() + (_history->overviewCount(_overview) - _history->overview[_overview].size());
			newMigrated = false;
		}
		if (newIndex >= 0 && newIndex < (newMigrated ? _migrated : _history)->overview[_overview].size()) {
			if (HistoryItem *item = App::histItemById(newMigrated ? 0 : _channel, (newMigrated ? _migrated : _history)->overview[_overview][newIndex])) {
				_index = newIndex;
				_msgid = item->id;
				_msgmigrated = (item->history() == _migrated);
				_channel = _history ? _history->channelId() : NoChannel;
				_canForward = _msgid > 0;
				_canDelete = item->canDelete();
				stopGif();
				if (HistoryMedia *media = item->getMedia()) {
					switch (media->type()) {
					case MediaTypePhoto: displayPhoto(static_cast<HistoryPhoto*>(item->getMedia())->photo(), item); preloadData(delta); break;
					case MediaTypeFile:
					case MediaTypeVideo:
					case MediaTypeGif:
					case MediaTypeSticker: displayDocument(media->getDocument(), item); preloadData(delta); break;
					}
				} else {
					displayDocument(0, item);
					preloadData(delta);
				}
			}
		} else if (!newMigrated && newIndex == _history->overview[_overview].size() && _additionalChatPhoto) {
			_index = -1;
			_msgid = 0;
			_msgmigrated = false;
			_canForward = false;
			_canDelete = false;
			stopGif();
			displayPhoto(_additionalChatPhoto, 0);
		}
		if (delta < 0 && _index < MediaOverviewStartPerPage) {
			loadBack();
		}
	} else if (_user) {
		if (newIndex >= 0 && newIndex < _user->photos.size()) {
			_index = newIndex;
			displayPhoto(_user->photos[_index], 0);
			preloadData(delta);
		}
		if (delta > 0 && _index > _user->photos.size() - MediaOverviewStartPerPage) {
			loadBack();
		}
	}
	return true;
}

void MediaView::preloadData(int32 delta) {
	int indexInOverview = _index;
	bool indexOfMigratedItem = _msgmigrated;
	if (_index < 0) {
		if (_overview != OverviewChatPhotos || !_history) return;
		indexInOverview = _history->overview[OverviewChatPhotos].size();
		indexOfMigratedItem = false;
	}
	if (!_user && _overview == OverviewCount) return;

	int32 from = indexInOverview + (delta ? delta : -1), to = indexInOverview + (delta ? delta * MediaOverviewPreloadCount : 1);
	if (from > to) qSwap(from, to);
	if (_history && _overview != OverviewCount) {
		int32 forgetIndex = indexInOverview - delta * 2;
		History *forgetHistory = indexOfMigratedItem ? _migrated : _history;
		if (_migrated) {
			if (indexOfMigratedItem && forgetIndex >= _migrated->overview[_overview].size()) {
				forgetHistory = _history;
				forgetIndex -= _migrated->overview[_overview].size() + (_history->overviewCount(_overview) - _history->overview[_overview].size());
			} else if (!indexOfMigratedItem && forgetIndex < 0) {
				forgetHistory = _migrated;
				forgetIndex += _migrated->overview[_overview].size();
			}
		}
		if (forgetIndex >= 0 && forgetIndex < forgetHistory->overview[_overview].size() && (forgetHistory != (indexOfMigratedItem ? _migrated : _history) || forgetIndex != indexInOverview)) {
			if (HistoryItem *item = App::histItemById(forgetHistory->channelId(), forgetHistory->overview[_overview][forgetIndex])) {
				if (HistoryMedia *media = item->getMedia()) {
					switch (media->type()) {
					case MediaTypePhoto: static_cast<HistoryPhoto*>(media)->photo()->forget(); break;
					case MediaTypeFile:
					case MediaTypeVideo:
					case MediaTypeGif:
					case MediaTypeSticker: media->getDocument()->forget(); break;
					}
				}
			}
		}

		for (int32 i = from; i <= to; ++i) {
			History *previewHistory = indexOfMigratedItem ? _migrated : _history;
			int32 previewIndex = i;
			if (_migrated) {
				if (indexOfMigratedItem && previewIndex >= _migrated->overview[_overview].size()) {
					previewHistory = _history;
					previewIndex -= _migrated->overview[_overview].size() + (_history->overviewCount(_overview) - _history->overview[_overview].size());
				} else if (!indexOfMigratedItem && previewIndex < 0) {
					previewHistory = _migrated;
					previewIndex += _migrated->overview[_overview].size();
				}
			}
			if (previewIndex >= 0 && previewIndex < previewHistory->overview[_overview].size() && (previewHistory != (indexOfMigratedItem ? _migrated : _history) || previewIndex != indexInOverview)) {
				if (HistoryItem *item = App::histItemById(previewHistory->channelId(), previewHistory->overview[_overview][previewIndex])) {
					if (HistoryMedia *media = item->getMedia()) {
						switch (media->type()) {
						case MediaTypePhoto: static_cast<HistoryPhoto*>(media)->photo()->download(); break;
						case MediaTypeFile:
						case MediaTypeVideo:
						case MediaTypeGif: {
							DocumentData *doc = media->getDocument();
							doc->thumb->load();
							doc->automaticLoad(item);
						} break;
						case MediaTypeSticker: media->getDocument()->sticker()->img->load(); break;
						}
					}
				}
			}
		}
	} else if (_user) {
		for (int32 i = from; i <= to; ++i) {
			if (i >= 0 && i < _user->photos.size() && i != indexInOverview) {
				_user->photos[i]->thumb->load();
			}
		}
		for (int32 i = from; i <= to; ++i) {
			if (i >= 0 && i < _user->photos.size() && i != indexInOverview) {
				_user->photos[i]->download();
			}
		}
		int32 forgetIndex = indexInOverview - delta * 2;
		if (forgetIndex >= 0 && forgetIndex < _user->photos.size() && forgetIndex != indexInOverview) {
			_user->photos[forgetIndex]->forget();
		}
	}
}

void MediaView::mousePressEvent(QMouseEvent *e) {
	updateOver(e->pos());
	if (_menu || !_receiveMouse) return;

	ClickHandler::pressed();

	if (e->button() == Qt::LeftButton) {
		_down = OverNone;
		if (!ClickHandler::getPressed()) {
			if (_over == OverLeftNav && moveToNext(-1)) {
				_lastAction = e->pos();
			} else if (_over == OverRightNav && moveToNext(1)) {
				_lastAction = e->pos();
			} else if (_over == OverName) {
				_down = OverName;
			} else if (_over == OverDate) {
				_down = OverDate;
			} else if (_over == OverHeader) {
				_down = OverHeader;
			} else if (_over == OverSave) {
				_down = OverSave;
			} else if (_over == OverIcon) {
				_down = OverIcon;
			} else if (_over == OverMore) {
				_down = OverMore;
			} else if (_over == OverClose) {
				_down = OverClose;
			} else if (_over == OverVideo) {
				_down = OverVideo;
			} else if (!_saveMsg.contains(e->pos()) || !_saveMsgStarted) {
				_pressed = true;
				_dragging = 0;
				updateCursor();
				_mStart = e->pos();
				_xStart = _x;
				_yStart = _y;
			}
		}
	} else if (e->button() == Qt::MiddleButton) {
		zoomReset();
	}
	activateControls();
}

void MediaView::mouseDoubleClickEvent(QMouseEvent *e) {
	updateOver(e->pos());

	if (_over == OverVideo) {
		onVideoToggleFullScreen();
		onVideoPauseResume();
	} else {
		e->ignore();
		return TWidget::mouseDoubleClickEvent(e);
	}
}

void MediaView::snapXY() {
	int32 xmin = width() - _w, xmax = 0;
	int32 ymin = height() - _h, ymax = 0;
	if (xmin > (width() - _w) / 2) xmin = (width() - _w) / 2;
	if (xmax < (width() - _w) / 2) xmax = (width() - _w) / 2;
	if (ymin > (height() - _h) / 2) ymin = (height() - _h) / 2;
	if (ymax < (height() - _h) / 2) ymax = (height() - _h) / 2;
	if (_x < xmin) _x = xmin;
	if (_x > xmax) _x = xmax;
	if (_y < ymin) _y = ymin;
	if (_y > ymax) _y = ymax;
}

void MediaView::mouseMoveEvent(QMouseEvent *e) {
	updateOver(e->pos());
	if (_lastAction.x() >= 0 && (e->pos() - _lastAction).manhattanLength() >= st::mvDeltaFromLastAction) {
		_lastAction = QPoint(-st::mvDeltaFromLastAction, -st::mvDeltaFromLastAction);
	}
	if (_pressed) {
		if (!_dragging && (e->pos() - _mStart).manhattanLength() >= QApplication::startDragDistance()) {
			_dragging = QRect(_x, _y, _w, _h).contains(_mStart) ? 1 : -1;
			if (_dragging > 0) {
				if (_w > width() || _h > height()) {
					setCursor(style::cur_sizeall);
				} else {
					setCursor(style::cur_default);
				}
			}
		}
		if (_dragging > 0) {
			_x = _xStart + (e->pos() - _mStart).x();
			_y = _yStart + (e->pos() - _mStart).y();
			snapXY();
			update();
		}
	}
}

void MediaView::updateOverRect(OverState state) {
	switch (state) {
	case OverLeftNav: update(_leftNav); break;
	case OverRightNav: update(_rightNav); break;
	case OverName: update(_nameNav); break;
	case OverDate: update(_dateNav); break;
	case OverSave: update(_saveNavIcon); break;
	case OverIcon: update(_docIconRect); break;
	case OverHeader: update(_headerNav); break;
	case OverClose: update(_closeNav); break;
	case OverMore: update(_moreNavIcon); break;
	}
}

bool MediaView::updateOverState(OverState newState) {
	bool result = true;
	if (_over != newState) {
		if (newState == OverMore && !_ignoringDropdown) {
			QTimer::singleShot(0, this, SLOT(onDropdown()));
		}
		updateOverRect(_over);
		updateOverRect(newState);
		if (_over != OverNone) {
			_animations[_over] = getms();
			ShowingOpacities::iterator i = _animOpacities.find(_over);
			if (i != _animOpacities.end()) {
				i->start(0);
			} else {
				_animOpacities.insert(_over, anim::fvalue(1, 0));
			}
			if (!_a_state.animating()) _a_state.start();
		} else {
			result = false;
		}
		_over = newState;
		if (newState != OverNone) {
			_animations[_over] = getms();
			ShowingOpacities::iterator i = _animOpacities.find(_over);
			if (i != _animOpacities.end()) {
				i->start(1);
			} else {
				_animOpacities.insert(_over, anim::fvalue(0, 1));
			}
			if (!_a_state.animating()) _a_state.start();
		}
		updateCursor();
	}
	return result;
}

void MediaView::updateOver(QPoint pos) {
	ClickHandlerPtr lnk;
	ClickHandlerHost *lnkhost = nullptr;

	if (_saveMsgStarted && _saveMsg.contains(pos)) {
		auto textState = _saveMsgText.getState(pos.x() - _saveMsg.x() - st::medviewSaveMsgPadding.left(), pos.y() - _saveMsg.y() - st::medviewSaveMsgPadding.top(), _saveMsg.width() - st::medviewSaveMsgPadding.left() - st::medviewSaveMsgPadding.right());
		lnk = textState.link;
		lnkhost = this;
	} else if (_captionRect.contains(pos)) {
		auto textState = _caption.getState(pos.x() - _captionRect.x(), pos.y() - _captionRect.y(), _captionRect.width());
		lnk = textState.link;
		lnkhost = this;
	}

	// retina
	if (pos.x() == width()) {
		pos.setX(pos.x() - 1);
	}
	if (pos.y() == height()) {
		pos.setY(pos.y() - 1);
	}

	ClickHandler::setActive(lnk, lnkhost);

	if (_pressed || _dragging) return;

	if (_fullScreenVideo) {
		updateOverState(OverVideo);
	} else if (_leftNavVisible && _leftNav.contains(pos)) {
		updateOverState(OverLeftNav);
	} else if (_rightNavVisible && _rightNav.contains(pos)) {
		updateOverState(OverRightNav);
	} else if (_nameNav.contains(pos)) {
		updateOverState(OverName);
	} else if (_msgid && _dateNav.contains(pos)) {
		updateOverState(OverDate);
	} else if (_headerHasLink && _headerNav.contains(pos)) {
		updateOverState(OverHeader);
	} else if (_saveVisible && _saveNav.contains(pos)) {
		updateOverState(OverSave);
	} else if (_doc && !fileShown() && _docIconRect.contains(pos)) {
		updateOverState(OverIcon);
	} else if (_moreNav.contains(pos)) {
		updateOverState(OverMore);
	} else if (_closeNav.contains(pos)) {
		updateOverState(OverClose);
	} else if (_doc && fileShown() && QRect(_x, _y, _w, _h).contains(pos)) {
		if (_doc->isVideo() && _gif) {
			updateOverState(OverVideo);
		} else if (!_doc->loaded()) {
			updateOverState(OverIcon);
		} else if (_over != OverNone) {
			updateOverState(OverNone);
		}
	} else if (_over != OverNone) {
		updateOverState(OverNone);
	}
}

void MediaView::mouseReleaseEvent(QMouseEvent *e) {
	updateOver(e->pos());

	if (ClickHandlerPtr activated = ClickHandler::unpressed()) {
		App::activateClickHandler(activated, e->button());
		return;
	}

	if (_over == OverName && _down == OverName) {
		if (App::wnd() && _from) {
			close();
			Ui::showPeerProfile(_from);
		}
	} else if (_over == OverDate && _down == OverDate) {
		onToMessage();
	} else if (_over == OverHeader && _down == OverHeader) {
		onOverview();
	} else if (_over == OverSave && _down == OverSave) {
		onDownload();
	} else if (_over == OverIcon && _down == OverIcon) {
		onDocClick();
	} else if (_over == OverMore && _down == OverMore) {
		QTimer::singleShot(0, this, SLOT(onDropdown()));
	} else if (_over == OverClose && _down == OverClose) {
		close();
	} else if (_over == OverVideo && _down == OverVideo) {
		onVideoPauseResume();
	} else if (_pressed) {
		if (_dragging) {
			if (_dragging > 0) {
				_x = _xStart + (e->pos() - _mStart).x();
				_y = _yStart + (e->pos() - _mStart).y();
				snapXY();
				update();
			}
			_dragging = 0;
			setCursor(style::cur_default);
		} else if ((e->pos() - _lastAction).manhattanLength() >= st::mvDeltaFromLastAction && (!_doc || fileShown() || !_docRect.contains(e->pos()))) {
			close();
		}
		_pressed = false;
	}
	_down = OverNone;
	activateControls();
}

void MediaView::contextMenuEvent(QContextMenuEvent *e) {
	if (e->reason() != QContextMenuEvent::Mouse || QRect(_x, _y, _w, _h).contains(e->pos())) {
		if (_menu) {
			_menu->deleteLater();
			_menu = 0;
		}
		_menu = new PopupMenu(st::mvPopupMenu);
		updateDropdown();
		for (int32 i = 0, l = _btns.size(); i < l; ++i) {
			if (!_btns.at(i)->isHidden()) _menu->addAction(_btns.at(i)->getText(), _btns.at(i), SIGNAL(clicked()))->setEnabled(true);
		}
		connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroy(QObject*)));
		_menu->popup(e->globalPos());
		e->accept();
		activateControls();
	}
}

void MediaView::touchEvent(QTouchEvent *e) {
	switch (e->type()) {
	case QEvent::TouchBegin:
		if (_touchPress || e->touchPoints().isEmpty()) return;
		_touchTimer.start(QApplication::startDragTime());
		_touchPress = true;
		_touchMove = _touchRightButton = false;
		_touchStart = e->touchPoints().cbegin()->screenPos().toPoint();
		break;

	case QEvent::TouchUpdate:
		if (!_touchPress || e->touchPoints().isEmpty()) return;
		if (!_touchMove && (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart).manhattanLength() >= QApplication::startDragDistance()) {
			_touchMove = true;
		}
		break;

	case QEvent::TouchEnd:
		if (!_touchPress) return;
		if (!_touchMove && App::wnd()) {
			Qt::MouseButton btn(_touchRightButton ? Qt::RightButton : Qt::LeftButton);
			QPoint mapped(mapFromGlobal(_touchStart)), winMapped(App::wnd()->mapFromGlobal(_touchStart));

			QMouseEvent pressEvent(QEvent::MouseButtonPress, mapped, winMapped, _touchStart, btn, Qt::MouseButtons(btn), Qt::KeyboardModifiers());
			pressEvent.accept();
			mousePressEvent(&pressEvent);

			QMouseEvent releaseEvent(QEvent::MouseButtonRelease, mapped, winMapped, _touchStart, btn, Qt::MouseButtons(btn), Qt::KeyboardModifiers());
			mouseReleaseEvent(&releaseEvent);

			if (_touchRightButton) {
				QContextMenuEvent contextEvent(QContextMenuEvent::Mouse, mapped, _touchStart);
				contextMenuEvent(&contextEvent);
			}
		} else if (_touchMove) {
			if ((!_leftNavVisible || !_leftNav.contains(mapFromGlobal(_touchStart))) && (!_rightNavVisible || !_rightNav.contains(mapFromGlobal(_touchStart)))) {
				QPoint d = (e->touchPoints().cbegin()->screenPos().toPoint() - _touchStart);
				if (d.x() * d.x() > d.y() * d.y() && (d.x() > st::mvSwipeDistance || d.x() < -st::mvSwipeDistance)) {
					moveToNext(d.x() > 0 ? -1 : 1);
				}
			}
		}
		_touchTimer.stop();
		_touchPress = _touchMove = _touchRightButton = false;
		break;

	case QEvent::TouchCancel:
		_touchPress = false;
		_touchTimer.stop();
		break;
	}
}

bool MediaView::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			if (ev->type() != QEvent::TouchBegin || ev->touchPoints().isEmpty() || !childAt(mapFromGlobal(ev->touchPoints().cbegin()->screenPos().toPoint()))) {
				touchEvent(ev);
				return true;
			}
		}
	} else if (e->type() == QEvent::Wheel) {
		QWheelEvent *ev = static_cast<QWheelEvent*>(e);
		if (ev->phase() == Qt::ScrollBegin) {
			_accumScroll = ev->angleDelta();
		} else {
			_accumScroll += ev->angleDelta();
			if (ev->phase() == Qt::ScrollEnd) {
				if (ev->orientation() == Qt::Horizontal) {
					if (_accumScroll.x() * _accumScroll.x() > _accumScroll.y() * _accumScroll.y() && _accumScroll.x() != 0) {
						moveToNext(_accumScroll.x() > 0 ? -1 : 1);
					}
					_accumScroll = QPoint();
				}
			}
		}
	}
	return TWidget::event(e);
}

bool MediaView::eventFilter(QObject *obj, QEvent *e) {
	auto type = e->type();
	if ((type == QEvent::MouseMove || type == QEvent::MouseButtonPress || type == QEvent::MouseButtonRelease) && obj->isWidgetType()) {
		if (isAncestorOf(static_cast<QWidget*>(obj))) {
			auto mouseEvent = static_cast<QMouseEvent*>(e);
			auto mousePosition = mapFromGlobal(mouseEvent->globalPos());
			bool activate = (mousePosition != _lastMouseMovePos);
			_lastMouseMovePos = mousePosition;
			if (type == QEvent::MouseButtonPress) {
				_mousePressed = true;
				activate = true;
			} else if (type == QEvent::MouseButtonRelease) {
				_mousePressed = false;
				activate = true;
			}
			if (activate) activateControls();
		}
	}
	return TWidget::eventFilter(obj, e);
}

void MediaView::setVisible(bool visible) {
	if (!visible) {
		_controlsHideTimer.stop();
		_controlsState = ControlsShown;
		a_cOpacity = anim::fvalue(1, 1);
	}
	TWidget::setVisible(visible);
	if (visible) {
		Sandbox::installEventFilter(this);
	} else {
		Sandbox::removeEventFilter(this);

		stopGif();
		_radial.stop();
		Notify::clipStopperHidden(ClipStopperMediaview);
	}
}

void MediaView::onMenuDestroy(QObject *obj) {
	if (_menu == obj) {
		_menu = 0;
		activateControls();
	}
	_receiveMouse = false;
	QTimer::singleShot(0, this, SLOT(receiveMouse()));
}

void MediaView::receiveMouse() {
	_receiveMouse = true;
}

void MediaView::onDropdown() {
	updateDropdown();
	_dropdown.ignoreShow(false);
	_dropdown.showStart();
	_dropdown.setFocus();
}

void MediaView::onCheckActive() {
	if (App::wnd() && isVisible()) {
		if (App::wnd()->isActiveWindow() && App::wnd()->hasFocus()) {
			activateWindow();
			Sandbox::setActiveWindow(this);
			setFocus();
		}
	}
}

void MediaView::onTouchTimer() {
	_touchRightButton = true;
}

void MediaView::updateImage() {
	update(_saveMsg);
}

void MediaView::findCurrent() {
	if (_msgmigrated) {
		for (int i = 0, l = _migrated->overview[_overview].size(); i < l; ++i) {
			if (_migrated->overview[_overview].at(i) == _msgid) {
				_index = i;
				break;
			}
		}
		if (!_history->overviewCountLoaded(_overview)) {
			loadBack();
		} else if (_history->overviewLoaded(_overview) && !_migrated->overviewLoaded(_overview)) { // all loaded
			if (!_migrated->overviewCountLoaded(_overview) || (_index < 2 && _migrated->overviewCount(_overview) > 0)) {
				loadBack();
			}
		}
	} else {
		for (int i = 0, l = _history->overview[_overview].size(); i < l; ++i) {
			if (_history->overview[_overview].at(i) == _msgid) {
				_index = i;
				break;
			}
		}
		if (!_history->overviewLoaded(_overview)) {
			if (!_history->overviewCountLoaded(_overview) || (_index < 2 && _history->overviewCount(_overview) > 0) || (_index < 1 && _migrated && !_migrated->overviewLoaded(_overview))) {
				loadBack();
			}
		} else if (_index < 1 && _migrated && !_migrated->overviewLoaded(_overview)) {
			loadBack();
		}
		if (_migrated && !_migrated->overviewCountLoaded(_overview)) {
			App::main()->preloadOverview(_migrated->peer, _overview);
		}
	}
}

void MediaView::loadBack() {
	if (_loadRequest || (_overview == OverviewCount && !_user)) {
		return;
	}
	if (_index < 0 && (!_additionalChatPhoto || _photo != _additionalChatPhoto || !_history)) {
		return;
	}

	if (_history && _overview != OverviewCount && (!_history->overviewLoaded(_overview) || (_migrated && !_migrated->overviewLoaded(_overview)))) {
		if (App::main()) {
			if (_msgmigrated || (_migrated && _index == 0 && _history->overviewLoaded(_overview))) {
				App::main()->loadMediaBack(_migrated->peer, _overview);
			} else {
				App::main()->loadMediaBack(_history->peer, _overview);
				if (_migrated && _index == 0 && (_migrated->overviewCount(_overview) < 0 || _migrated->overview[_overview].isEmpty()) && !_migrated->overviewLoaded(_overview)) {
					App::main()->loadMediaBack(_migrated->peer, _overview);
				}
			}
			if (_msgmigrated && !_history->overviewCountLoaded(_overview)) {
				App::main()->preloadOverview(_history->peer, _overview);
			}
		}
	} else if (_user && _user->photosCount != 0) {
		int32 limit = (_index < MediaOverviewStartPerPage && _user->photos.size() > MediaOverviewStartPerPage) ? SearchPerPage : MediaOverviewStartPerPage;
		_loadRequest = MTP::send(MTPphotos_GetUserPhotos(_user->inputUser, MTP_int(_user->photos.size()), MTP_long(0), MTP_int(limit)), rpcDone(&MediaView::userPhotosLoaded, _user));
	}
}

void MediaView::generateTransparentBrush() {
	auto size = st::mediaviewTransparentSize * cIntRetinaFactor();
	auto transparent = QImage(2 * size, 2 * size, QImage::Format_ARGB32_Premultiplied);
	transparent.fill(st::mediaviewTransparentBg->c);
	{
		Painter p(&transparent);
		p.fillRect(rtlrect(0, size, size, size, 2 * size), st::mediaviewTransparentFg);
		p.fillRect(rtlrect(size, 0, size, size, 2 * size), st::mediaviewTransparentFg);
	}
	transparent.setDevicePixelRatio(cRetinaFactor());
	_transparentBrush = QBrush(transparent);
}

MediaView::LastChatPhoto MediaView::computeLastOverviewChatPhoto() {
	LastChatPhoto emptyResult = { nullptr, nullptr };
	auto lastPhotoInOverview = [&emptyResult](auto history, auto list) -> LastChatPhoto {
		if (auto item = App::histItemById(history->channelId(), list.back())) {
			if (auto media = item->getMedia()) {
				if (media->type() == MediaTypePhoto && !item->toHistoryMessage()) {
					return { item, static_cast<HistoryPhoto*>(media)->photo() };
				}
			}
		}
		return emptyResult;
	};

	if (!_history) return emptyResult;
	auto &list = _history->overview[OverviewChatPhotos];
	if (!list.isEmpty()) {
		return lastPhotoInOverview(_history, list);
	}

	if (!_migrated || !_history->overviewLoaded(OverviewChatPhotos)) return emptyResult;
	auto &migratedList = _migrated->overview[OverviewChatPhotos];
	if (!migratedList.isEmpty()) {
		return lastPhotoInOverview(_migrated, migratedList);
	}
	return emptyResult;
}

void MediaView::computeAdditionalChatPhoto(PeerData *peer, PhotoData *lastOverviewPhoto) {
	if (!peer->photoId || peer->photoId == UnknownPeerPhotoId) {
		_additionalChatPhoto = nullptr;
	} else if (lastOverviewPhoto && lastOverviewPhoto->id == peer->photoId) {
		_additionalChatPhoto = nullptr;
	} else {
		_additionalChatPhoto = App::photo(peer->photoId);
	}
}

void MediaView::userPhotosLoaded(UserData *u, const MTPphotos_Photos &photos, mtpRequestId req) {
	if (req == _loadRequest) {
		_loadRequest = 0;
	}

	const QVector<MTPPhoto> *v = 0;
	switch (photos.type()) {
	case mtpc_photos_photos: {
		const auto &d(photos.c_photos_photos());
		App::feedUsers(d.vusers);
		v = &d.vphotos.c_vector().v;
		u->photosCount = 0;
	} break;

	case mtpc_photos_photosSlice: {
		const auto &d(photos.c_photos_photosSlice());
		App::feedUsers(d.vusers);
		u->photosCount = d.vcount.v;
		v = &d.vphotos.c_vector().v;
	} break;

	default: return;
	}

	if (v->isEmpty()) {
		u->photosCount = 0;
	}

	for (QVector<MTPPhoto>::const_iterator i = v->cbegin(), e = v->cend(); i != e; ++i) {
		PhotoData *photo = App::feedPhoto(*i);
		photo->thumb->load();
		u->photos.push_back(photo);
	}
	if (App::wnd()) App::wnd()->mediaOverviewUpdated(u, OverviewCount);
}

void MediaView::updateHeader() {
	int32 index = _index, count = 0, addcount = (_migrated && _overview != OverviewCount) ? _migrated->overviewCount(_overview) : 0;
	if (_history) {
		if (_overview != OverviewCount) {
			bool lastOverviewPhotoLoaded = (!_history->overview[_overview].isEmpty() || (
				_migrated && _history->overviewCount(_overview) == 0 && !_migrated->overview[_overview].isEmpty()));
			count = _history->overviewCount(_overview);
			if (addcount >= 0 && count >= 0) {
				count += addcount;
			}
			if (index >= 0 && (_msgmigrated ? (count >= 0 && addcount >= 0 && _history->overviewLoaded(_overview)) : (count >= 0))) {
				if (_msgmigrated) {
					index += addcount - _migrated->overview[_overview].size();
				} else {
					index += count - _history->overview[_overview].size();
				}
				if (_additionalChatPhoto && lastOverviewPhotoLoaded) {
					++count;
				}
			} else if (index < 0 && _additionalChatPhoto && _photo == _additionalChatPhoto && lastOverviewPhotoLoaded) {
				// Additional chat photo (not in the list => place it at the end of the list).
				index = count;
				++count;
			} else {
				count = 0; // unknown yet
			}
		}
	} else if (_user) {
		count = _user->photosCount ? _user->photosCount : _user->photos.size();
	}
	if (index >= 0 && index < count && count > 1) {
		if (_doc) {
			_headerText = lng_mediaview_file_n_of_count(lt_file, _doc->name.isEmpty() ? lang(lng_mediaview_doc_image) : _doc->name, lt_n, QString::number(index + 1), lt_count, QString::number(count));
		} else {
			_headerText = lng_mediaview_n_of_count(lt_n, QString::number(index + 1), lt_count, QString::number(count));
		}
	} else {
		if (_doc) {
			_headerText = _doc->name.isEmpty() ? lang(lng_mediaview_doc_image) : _doc->name;
		} else if (_user) {
			_headerText = lang(lng_mediaview_profile_photo);
		} else if ((_channel && !_history->isMegagroup()) || (_peer && _peer->isChannel() && !_peer->isMegagroup())) {
			_headerText = lang(lng_mediaview_channel_photo);
		} else if (_peer) {
			_headerText = lang(lng_mediaview_group_photo);
		} else {
			_headerText = lang(lng_mediaview_single_photo);
		}
	}
	_headerHasLink = _history && typeHasMediaOverview(_overview);
	int32 hwidth = st::mvThickFont->width(_headerText);
	if (hwidth > width() / 3) {
		hwidth = width() / 3;
		_headerText = st::mvThickFont->elided(_headerText, hwidth, Qt::ElideMiddle);
	}
	_headerNav = myrtlrect(st::mvTextLeft, height() - st::mvHeaderTop, hwidth, st::mvThickFont->height);
}

float64 MediaView::overLevel(OverState control) const {
	ShowingOpacities::const_iterator i = _animOpacities.constFind(control);
	return (i == _animOpacities.cend()) ? (_over == control ? 1 : 0) : i->current();
}
