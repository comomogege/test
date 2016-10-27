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
#pragma once

#include "core/observer.h"
#include "ui/filedialog.h"

#include "settings/settings_block_widget.h"

class FlatLabel;
class LinkButton;

namespace Ui {
class RoundButton;
} // namespace Ui

namespace Notify {
struct PeerUpdate;
} // namespace Notify

namespace Profile {
class UserpicButton;
class CoverDropArea;
} // namespace Profile

namespace Settings {

class CoverWidget final : public BlockWidget {
	Q_OBJECT

public:
	CoverWidget(QWidget *parent, UserData *self);

	void showFinished();

private slots:
	void onPhotoShow();
	void onPhotoUploadStatusChanged(PeerId peerId = 0);
	void onCancelPhotoUpload();

	void onSetPhoto();
	void onEditName();

protected:
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dragLeaveEvent(QDragLeaveEvent *e) override;
	void dropEvent(QDropEvent *e) override;

	void paintContents(Painter &p) override;

	// Resizes content and counts natural widget height for the desired width.
	int resizeGetHeight(int newWidth) override;

private:
	// Observed notifications.
	void notifyPeerUpdated(const Notify::PeerUpdate &update);
	void notifyFileQueryUpdated(const FileDialog::QueryUpdate &update);

	PhotoData *validatePhoto() const;

	void refreshNameGeometry(int newWidth);
	void refreshNameText();
	void refreshStatusText();

	void paintDivider(Painter &p);

	void showSetPhotoBox(const QImage &img);
	void resizeDropArea();
	void dropAreaHidden(Profile::CoverDropArea *dropArea);
	bool mimeDataHasImage(const QMimeData *mimeData) const;

	UserData *_self;

	ChildWidget<Profile::UserpicButton> _userpicButton;
	ChildWidget<Profile::CoverDropArea> _dropArea = { nullptr };

	ChildWidget<FlatLabel> _name;
	ChildWidget<Ui::RoundButton> _editNameInline;
	ChildWidget<LinkButton> _cancelPhotoUpload = { nullptr };

	QPoint _statusPosition;
	QString _statusText;
	bool _statusTextIsOnline = false;

	ChildWidget<Ui::RoundButton> _setPhoto;
	ChildWidget<Ui::RoundButton> _editName;
	bool _editNameVisible = true;

	int _dividerTop = 0;

	FileDialog::QueryId _setPhotoFileQueryId = 0;

};

} // namespace Settings
