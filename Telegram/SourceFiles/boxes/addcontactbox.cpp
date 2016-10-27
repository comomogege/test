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

#include "application.h"
#include "addcontactbox.h"
#include "contactsbox.h"
#include "confirmbox.h"
#include "photocropbox.h"
#include "ui/filedialog.h"
#include "mainwidget.h"
#include "mainwindow.h"
#include "apiwrap.h"
#include "observer_peer.h"
#include "styles/style_boxes.h"
#include "styles/style_dialogs.h"

AddContactBox::AddContactBox(QString fname, QString lname, QString phone) : AbstractBox(st::boxWidth)
, _save(this, lang(lng_add_contact), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _retry(this, lang(lng_try_other_contact), st::defaultBoxButton)
, _first(this, st::defaultInputField, lang(lng_signup_firstname), fname)
, _last(this, st::defaultInputField, lang(lng_signup_lastname), lname)
, _phone(this, st::defaultInputField, lang(lng_contact_phone), phone)
, _invertOrder(langFirstNameGoesSecond()) {
	if (!phone.isEmpty()) {
		_phone.setDisabled(true);
	}

	initBox();
}

AddContactBox::AddContactBox(UserData *user) : AbstractBox(st::boxWidth)
, _user(user)
, _save(this, lang(lng_settings_save), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _retry(this, lang(lng_try_other_contact), st::defaultBoxButton)
, _first(this, st::defaultInputField, lang(lng_signup_firstname), user->firstName)
, _last(this, st::defaultInputField, lang(lng_signup_lastname), user->lastName)
, _phone(this, st::defaultInputField, lang(lng_contact_phone), user->phone())
, _invertOrder(langFirstNameGoesSecond()) {
	_phone.setDisabled(true);
	initBox();
}

void AddContactBox::initBox() {
	if (_invertOrder) {
		setTabOrder(&_last, &_first);
	}
	if (_user) {
		_boxTitle = lang(lng_edit_contact_title);
	} else {
		bool readyToAdd = !_phone.getLastText().isEmpty() && (!_first.getLastText().isEmpty() || !_last.getLastText().isEmpty());
		_boxTitle = lang(readyToAdd ? lng_confirm_contact_data : lng_enter_contact_data);
	}
	setMaxHeight(st::boxTitleHeight + st::contactPadding.top() + _first.height() + st::contactSkip + _last.height() + st::contactPhoneSkip + _phone.height() + st::contactPadding.bottom() + st::boxPadding.bottom() + st::boxButtonPadding.top() + _save.height() + st::boxButtonPadding.bottom());
	_retry.hide();

	connect(&_save, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));
	connect(&_retry, SIGNAL(clicked()), this, SLOT(onRetry()));

	connect(&_first, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(&_last, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(&_phone, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));

	prepare();
}

void AddContactBox::showAll() {
	_first.show();
	_last.show();
	_phone.show();
	_save.show();
	_cancel.show();
}

void AddContactBox::doSetInnerFocus() {
	if ((_first.getLastText().isEmpty() && _last.getLastText().isEmpty()) || !_phone.isEnabled()) {
		(_invertOrder ? _last : _first).setFocus();
	} else {
		_phone.setFocus();
	}
}

void AddContactBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, _boxTitle);

	if (_retry.isHidden()) {
		st::contactUserIcon.paint(p, st::boxPadding.left(), _first.y() + st::contactIconTop, width());
		st::contactPhoneIcon.paint(p, st::boxPadding.left(), _phone.y() + st::contactIconTop, width());
	} else {
		p.setPen(st::black->p);
		p.setFont(st::boxTextFont->f);
		int32 h = height() - st::boxTitleHeight - st::contactPadding.top() - st::contactPadding.bottom() - st::boxPadding.bottom() - st::boxButtonPadding.top() - _retry.height() - st::boxButtonPadding.bottom();
		p.drawText(QRect(st::boxPadding.left(), st::boxTitleHeight + st::contactPadding.top(), width() - st::boxPadding.left() - st::boxPadding.right(), h), lng_contact_not_joined(lt_name, _sentName), style::al_topleft);
	}
}

void AddContactBox::resizeEvent(QResizeEvent *e) {
	_first.resize(width() - st::boxPadding.left() - st::contactPadding.left() - st::boxPadding.right(), _first.height());
	_last.resize(_first.width(), _last.height());
	_phone.resize(_first.width(), _last.height());
	if (_invertOrder) {
		_last.moveToLeft(st::boxPadding.left() + st::contactPadding.left(), st::boxTitleHeight + st::contactPadding.top());
		_first.moveToLeft(st::boxPadding.left() + st::contactPadding.left(), _last.y() + _last.height() + st::contactSkip);
		_phone.moveToLeft(st::boxPadding.left() + st::contactPadding.left(), _first.y() + _first.height() + st::contactPhoneSkip);
	} else {
		_first.moveToLeft(st::boxPadding.left() + st::contactPadding.left(), st::boxTitleHeight + st::contactPadding.top());
		_last.moveToLeft(st::boxPadding.left() + st::contactPadding.left(), _first.y() + _first.height() + st::contactSkip);
		_phone.moveToLeft(st::boxPadding.left() + st::contactPadding.left(), _last.y() + _last.height() + st::contactPhoneSkip);
	}

	_save.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _save.height());
	_retry.moveToRight(st::boxButtonPadding.right(), _save.y());
	_cancel.moveToRight(st::boxButtonPadding.right() + (_retry.isHidden() ? _save.width() : _retry.width()) + st::boxButtonPadding.left(), _save.y());
	AbstractBox::resizeEvent(e);
}

void AddContactBox::onSubmit() {
	if (_first.hasFocus()) {
		_last.setFocus();
	} else if (_last.hasFocus()) {
		if (_phone.isEnabled()) {
			_phone.setFocus();
		} else {
			onSave();
		}
	} else if (_phone.hasFocus()) {
		onSave();
	}
}

void AddContactBox::onSave() {
	if (_addRequest) return;

	QString firstName = prepareText(_first.getLastText());
	QString lastName = prepareText(_last.getLastText());
	QString phone = _phone.getLastText().trimmed();
	if (firstName.isEmpty() && lastName.isEmpty()) {
		if (_invertOrder) {
			_last.setFocus();
			_last.showError();
		} else {
			_first.setFocus();
			_first.showError();
		}
		return;
	} else if (!_user && !App::isValidPhone(phone)) {
		_phone.setFocus();
		_phone.showError();
		return;
	}
	if (firstName.isEmpty()) {
		firstName = lastName;
		lastName = QString();
	}
	_sentName = firstName;
	if (_user) {
		_contactId = rand_value<uint64>();
		QVector<MTPInputContact> v(1, MTP_inputPhoneContact(MTP_long(_contactId), MTP_string(_user->phone()), MTP_string(firstName), MTP_string(lastName)));
		_addRequest = MTP::send(MTPcontacts_ImportContacts(MTP_vector<MTPInputContact>(v), MTP_bool(false)), rpcDone(&AddContactBox::onSaveUserDone), rpcFail(&AddContactBox::onSaveUserFail));
	} else {
		_contactId = rand_value<uint64>();
		QVector<MTPInputContact> v(1, MTP_inputPhoneContact(MTP_long(_contactId), MTP_string(phone), MTP_string(firstName), MTP_string(lastName)));
		_addRequest = MTP::send(MTPcontacts_ImportContacts(MTP_vector<MTPInputContact>(v), MTP_bool(false)), rpcDone(&AddContactBox::onImportDone));
	}
}

bool AddContactBox::onSaveUserFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_addRequest = 0;
	QString err(error.type());
	QString firstName = _first.getLastText().trimmed(), lastName = _last.getLastText().trimmed();
	if (err == "CHAT_TITLE_NOT_MODIFIED") {
		_user->setName(firstName, lastName, _user->nameOrPhone, _user->username);
		onClose();
		return true;
	} else if (err == "NO_CHAT_TITLE") {
		_first.setFocus();
		_first.showError();
		return true;
	}
	_first.setFocus();
	return true;
}

void AddContactBox::onImportDone(const MTPcontacts_ImportedContacts &res) {
	if (isHidden() || !App::main()) return;

	const auto &d(res.c_contacts_importedContacts());
	App::feedUsers(d.vusers);

	const auto &v(d.vimported.c_vector().v);
	UserData *user = nullptr;
	if (!v.isEmpty()) {
		const auto &c(v.front().c_importedContact());
		if (c.vclient_id.v != _contactId) return;

		user = App::userLoaded(c.vuser_id.v);
	}
	if (user) {
		Notify::userIsContactChanged(user, true);
		Ui::hideLayer();
	} else {
		_save.hide();
		_first.hide();
		_last.hide();
		_phone.hide();
		_retry.show();
		resizeEvent(0);
		update();
	}
}

void AddContactBox::onSaveUserDone(const MTPcontacts_ImportedContacts &res) {
	auto &d = res.c_contacts_importedContacts();
	App::feedUsers(d.vusers);
	onClose();
}

void AddContactBox::onRetry() {
	_addRequest = 0;
	_contactId = 0;
	_save.show();
	_retry.hide();
	resizeEvent(0);
	showAll();
	_first.setText(QString());
	_first.updatePlaceholder();
	_last.setText(QString());
	_last.updatePlaceholder();
	_phone.clearText();
	_phone.setDisabled(false);
	_first.setFocus();
	update();
}

NewGroupBox::NewGroupBox() : AbstractBox(),
_group(this, qsl("group_type"), 0, lang(lng_create_group_title), true),
_channel(this, qsl("group_type"), 1, lang(lng_create_channel_title)),
_aboutGroupWidth(width() - st::boxPadding.left() - st::boxButtonPadding.right() - st::newGroupPadding.left() - st::defaultRadiobutton.textPosition.x()),
_aboutGroup(st::normalFont, lng_create_group_about(lt_count, Global::MegagroupSizeMax()), _defaultOptions, _aboutGroupWidth),
_aboutChannel(st::normalFont, lang(lng_create_channel_about), _defaultOptions, _aboutGroupWidth),
_next(this, lang(lng_create_group_next), st::defaultBoxButton),
_cancel(this, lang(lng_cancel), st::cancelBoxButton) {
	_aboutGroupHeight = _aboutGroup.countHeight(_aboutGroupWidth);
	setMaxHeight(st::boxPadding.top() + st::newGroupPadding.top() + _group.height() + _aboutGroupHeight + st::newGroupSkip + _channel.height() + _aboutChannel.countHeight(_aboutGroupWidth) + st::newGroupPadding.bottom() + st::boxPadding.bottom() + st::boxButtonPadding.top() + _next.height() + st::boxButtonPadding.bottom());

	connect(&_next, SIGNAL(clicked()), this, SLOT(onNext()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	prepare();
}

void NewGroupBox::showAll() {
	_group.show();
	_channel.show();
	_cancel.show();
	_next.show();
}

void NewGroupBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		onNext();
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void NewGroupBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	p.setPen(st::newGroupAboutFg->p);

	QRect aboutGroup(st::boxPadding.left() + st::newGroupPadding.left() + st::defaultRadiobutton.textPosition.x(), _group.y() + _group.height() + st::lineWidth, _aboutGroupWidth, _aboutGroupHeight);
	_aboutGroup.drawLeft(p, aboutGroup.x(), aboutGroup.y(), aboutGroup.width(), width());

	QRect aboutChannel(st::boxPadding.left() + st::newGroupPadding.left() + st::defaultRadiobutton.textPosition.x(), _channel.y() + _channel.height() + st::lineWidth, _aboutGroupWidth, _aboutGroupHeight);
	_aboutChannel.drawLeft(p, aboutChannel.x(), aboutChannel.y(), aboutChannel.width(), width());
}

void NewGroupBox::resizeEvent(QResizeEvent *e) {
	_group.moveToLeft(st::boxPadding.left() + st::newGroupPadding.left(), st::boxPadding.top() + st::newGroupPadding.top());
	_channel.moveToLeft(st::boxPadding.left() + st::newGroupPadding.left(), _group.y() + _group.height() + _aboutGroupHeight + st::newGroupSkip);

	_next.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _next.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _next.width() + st::boxButtonPadding.left(), _next.y());
	AbstractBox::resizeEvent(e);
}

void NewGroupBox::onNext() {
	Ui::showLayer(new GroupInfoBox(_group.checked() ? CreatingGroupGroup : CreatingGroupChannel, true), KeepOtherLayers);
}

GroupInfoBox::GroupInfoBox(CreatingGroupType creating, bool fromTypeChoose) : AbstractBox(),
_creating(creating),
a_photoOver(0, 0),
_a_photoOver(animation(this, &GroupInfoBox::step_photoOver)),
_photoOver(false),
_title(this, st::defaultInputField, lang(_creating == CreatingGroupChannel ? lng_dlg_new_channel_name : lng_dlg_new_group_name)),
_description(this, st::newGroupDescription, lang(lng_create_group_description)),
_next(this, lang(_creating == CreatingGroupChannel ? lng_create_group_create : lng_create_group_next), st::defaultBoxButton),
_cancel(this, lang(fromTypeChoose ? lng_create_group_back : lng_cancel), st::cancelBoxButton),
_creationRequestId(0), _createdChannel(0) {

	setMouseTracking(true);

	_title.setMaxLength(MaxGroupChannelTitle);

	_description.setMaxLength(MaxChannelDescription);
	_description.resize(width() - st::boxPadding.left() - st::newGroupInfoPadding.left() - st::boxPadding.right(), _description.height());

	updateMaxHeight();
	connect(&_description, SIGNAL(resized()), this, SLOT(onDescriptionResized()));
	connect(&_description, SIGNAL(submitted(bool)), this, SLOT(onNext()));
	connect(&_description, SIGNAL(cancelled()), this, SLOT(onClose()));

	connect(&_title, SIGNAL(submitted(bool)), this, SLOT(onNameSubmit()));

	connect(&_next, SIGNAL(clicked()), this, SLOT(onNext()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	subscribe(FileDialog::QueryDone(), [this](const FileDialog::QueryUpdate &update) {
		notifyFileQueryUpdated(update);
	});

	prepare();
}

void GroupInfoBox::showAll() {
	_title.show();
	if (_creating == CreatingGroupChannel) {
		_description.show();
	} else {
		_description.hide();
	}
	_cancel.show();
	_next.show();
}

void GroupInfoBox::doSetInnerFocus() {
	_title.setFocus();
}

void GroupInfoBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	QRect phRect(photoRect());
	if (phRect.intersects(e->rect())) {
		if (_photoSmall.isNull()) {
			float64 o = a_photoOver.current();
			if (o > 0) {
				if (o < 1) {
					QColor c;
					c.setRedF(st::newGroupPhotoBg->c.redF() * (1. - o) + st::newGroupPhotoBgOver->c.redF() * o);
					c.setGreenF(st::newGroupPhotoBg->c.greenF() * (1. - o) + st::newGroupPhotoBgOver->c.greenF() * o);
					c.setBlueF(st::newGroupPhotoBg->c.blueF() * (1. - o) + st::newGroupPhotoBgOver->c.blueF() * o);
					p.fillRect(phRect, c);
				} else {
					p.fillRect(phRect, st::newGroupPhotoBgOver->b);
				}
			} else {
				p.fillRect(phRect, st::newGroupPhotoBg->b);
			}
			p.drawSprite(phRect.topLeft() + st::newGroupPhotoIconPosition, st::newGroupPhotoIcon);
		} else {
			p.drawPixmap(phRect.topLeft(), _photoSmall);
		}
		if (phRect.contains(e->rect())) {
			return;
		}
	}
}

void GroupInfoBox::resizeEvent(QResizeEvent *e) {
	int32 nameLeft = st::newGroupPhotoSize + st::newGroupNamePosition.x();
	_title.resize(width() - st::boxPadding.left() - st::newGroupInfoPadding.left() - st::boxPadding.right() - nameLeft, _title.height());
	_title.moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left() + nameLeft, st::boxPadding.top() + st::newGroupInfoPadding.top() + st::newGroupNamePosition.y());

	_description.moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), st::boxPadding.top() + st::newGroupInfoPadding.top() + st::newGroupPhotoSize + st::newGroupDescriptionPadding.top());

	_next.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _next.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _next.width() + st::boxButtonPadding.left(), _next.y());
	AbstractBox::resizeEvent(e);
}

void GroupInfoBox::mouseMoveEvent(QMouseEvent *e) {
	updateSelected(e->globalPos());
}

void GroupInfoBox::updateSelected(const QPoint &cursorGlobalPosition) {
	QPoint p(mapFromGlobal(cursorGlobalPosition));

	bool photoOver = photoRect().contains(p);
	if (photoOver != _photoOver) {
		_photoOver = photoOver;
		if (_photoSmall.isNull()) {
			a_photoOver.start(_photoOver ? 1 : 0);
			_a_photoOver.start();
		}
	}

	setCursor(_photoOver ? style::cur_pointer : style::cur_default);
}

void GroupInfoBox::mousePressEvent(QMouseEvent *e) {
	mouseMoveEvent(e);
	if (_photoOver) {
		onPhoto();
	}
}

void GroupInfoBox::leaveEvent(QEvent *e) {
	updateSelected(QCursor::pos());
}

void GroupInfoBox::step_photoOver(float64 ms, bool timer) {
	float64 dt = ms / st::setPhotoDuration;
	if (dt >= 1) {
		_a_photoOver.stop();
		a_photoOver.finish();
	} else {
		a_photoOver.update(dt, anim::linear);
	}
	if (timer) update(photoRect());
}

void GroupInfoBox::onNameSubmit() {
	if (_title.getLastText().trimmed().isEmpty()) {
		_title.setFocus();
		_title.showError();
	} else if (_description.isHidden()) {
		onNext();
	} else {
		_description.setFocus();
	}
}

void GroupInfoBox::onNext() {
	if (_creationRequestId) return;

	QString title = prepareText(_title.getLastText()), description = prepareText(_description.getLastText(), true);
	if (title.isEmpty()) {
		_title.setFocus();
		_title.showError();
		return;
	}
	if (_creating == CreatingGroupGroup) {
		Ui::showLayer(new ContactsBox(title, _photoBig), KeepOtherLayers);
	} else {
		bool mega = false;
		MTPchannels_CreateChannel::Flags flags = mega ? MTPchannels_CreateChannel::Flag::f_megagroup : MTPchannels_CreateChannel::Flag::f_broadcast;
		_creationRequestId = MTP::send(MTPchannels_CreateChannel(MTP_flags(flags), MTP_string(title), MTP_string(description)), rpcDone(&GroupInfoBox::creationDone), rpcFail(&GroupInfoBox::creationFail));
	}
}

void GroupInfoBox::creationDone(const MTPUpdates &updates) {
	App::main()->sentUpdatesReceived(updates);

	const QVector<MTPChat> *v = 0;
	switch (updates.type()) {
	case mtpc_updates: v = &updates.c_updates().vchats.c_vector().v; break;
	case mtpc_updatesCombined: v = &updates.c_updatesCombined().vchats.c_vector().v; break;
	default: LOG(("API Error: unexpected update cons %1 (GroupInfoBox::creationDone)").arg(updates.type())); break;
	}

	ChannelData *channel = 0;
	if (v && !v->isEmpty() && v->front().type() == mtpc_channel) {
		channel = App::channel(v->front().c_channel().vid.v);
		if (channel) {
			if (!_photoBig.isNull()) {
				App::app()->uploadProfilePhoto(_photoBig, channel->id);
			}
			_createdChannel = channel;
			_creationRequestId = MTP::send(MTPchannels_ExportInvite(_createdChannel->inputChannel), rpcDone(&GroupInfoBox::exportDone));
			return;
		}
	} else {
		LOG(("API Error: channel not found in updates (GroupInfoBox::creationDone)"));
	}

	onClose();
}

bool GroupInfoBox::creationFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_creationRequestId = 0;
	if (error.type() == "NO_CHAT_TITLE") {
		_title.setFocus();
		_title.showError();
		return true;
	} else if (error.type() == qstr("USER_RESTRICTED")) {
		Ui::showLayer(new InformBox(lang(lng_cant_do_this)));
		return true;
	}
	return false;
}

void GroupInfoBox::exportDone(const MTPExportedChatInvite &result) {
	_creationRequestId = 0;
	if (result.type() == mtpc_chatInviteExported) {
		_createdChannel->setInviteLink(qs(result.c_chatInviteExported().vlink));
	}
	Ui::showLayer(new SetupChannelBox(_createdChannel));
}

void GroupInfoBox::onDescriptionResized() {
	updateMaxHeight();
	update();
}

QRect GroupInfoBox::photoRect() const {
	return myrtlrect(st::boxPadding.left() + st::newGroupInfoPadding.left(), st::boxPadding.top() + st::newGroupInfoPadding.top(), st::newGroupPhotoSize, st::newGroupPhotoSize);
}

void GroupInfoBox::updateMaxHeight() {
	int32 h = st::boxPadding.top() + st::newGroupInfoPadding.top() + st::newGroupPhotoSize + st::boxPadding.bottom() + st::newGroupInfoPadding.bottom() + st::boxButtonPadding.top() + _next.height() + st::boxButtonPadding.bottom();
	if (_creating == CreatingGroupChannel) {
		h += st::newGroupDescriptionPadding.top() + _description.height() + st::newGroupDescriptionPadding.bottom();
	}
	setMaxHeight(h);
}

void GroupInfoBox::onPhoto() {
	auto imgExtensions = cImgExtensions();
	auto filter = qsl("Image files (*") + imgExtensions.join(qsl(" *")) + qsl(");;") + filedialogAllFilesFilter();
	_setPhotoFileQueryId = FileDialog::queryReadFile(lang(lng_choose_images), filter);
}

void GroupInfoBox::notifyFileQueryUpdated(const FileDialog::QueryUpdate &update) {
	if (_setPhotoFileQueryId != update.queryId) {
		return;
	}
	_setPhotoFileQueryId = 0;

	QImage img;
	if (!update.remoteContent.isEmpty()) {
		img = App::readImage(update.remoteContent);
	} else {
		img = App::readImage(update.filePaths.front());
	}
	if (img.isNull() || img.width() > 10 * img.height() || img.height() > 10 * img.width()) {
		return;
	}
	PhotoCropBox *box = new PhotoCropBox(img, (_creating == CreatingGroupChannel) ? peerFromChannel(0) : peerFromChat(0));
	connect(box, SIGNAL(ready(const QImage&)), this, SLOT(onPhotoReady(const QImage&)));
	Ui::showLayer(box, KeepOtherLayers);
}

void GroupInfoBox::onPhotoReady(const QImage &img) {
	_photoBig = img;
	_photoSmall = App::pixmapFromImageInPlace(img.scaled(st::newGroupPhotoSize * cIntRetinaFactor(), st::newGroupPhotoSize * cIntRetinaFactor(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
	_photoSmall.setDevicePixelRatio(cRetinaFactor());
}

SetupChannelBox::SetupChannelBox(ChannelData *channel, bool existing) : AbstractBox()
, _channel(channel)
, _existing(existing)
, _public(this, qsl("channel_privacy"), 0, lang(channel->isMegagroup() ? lng_create_public_group_title : lng_create_public_channel_title), true)
, _private(this, qsl("channel_privacy"), 1, lang(channel->isMegagroup() ? lng_create_private_group_title : lng_create_private_channel_title))
, _aboutPublicWidth(width() - st::boxPadding.left() - st::boxButtonPadding.right() - st::newGroupPadding.left() - st::defaultRadiobutton.textPosition.x())
, _aboutPublic(st::normalFont, lang(channel->isMegagroup() ? lng_create_public_group_about : lng_create_public_channel_about), _defaultOptions, _aboutPublicWidth)
, _aboutPrivate(st::normalFont, lang(channel->isMegagroup() ? lng_create_private_group_about : lng_create_private_channel_about), _defaultOptions, _aboutPublicWidth)
, _link(this, st::defaultInputField, QString(), channel->username, true)
, _linkOver(false)
, _save(this, lang(lng_settings_save), st::defaultBoxButton)
, _skip(this, lang(existing ? lng_cancel : lng_create_group_skip), st::cancelBoxButton)
, a_goodOpacity(0, 0)
, _a_goodFade(animation(this, &SetupChannelBox::step_goodFade)) {
	setMouseTracking(true);

	_checkRequestId = MTP::send(MTPchannels_CheckUsername(_channel->inputChannel, MTP_string("preston")), RPCDoneHandlerPtr(), rpcFail(&SetupChannelBox::onFirstCheckFail));

	_aboutPublicHeight = _aboutPublic.countHeight(_aboutPublicWidth);
	updateMaxHeight();

	connect(&_save, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_skip, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(&_link, SIGNAL(changed()), this, SLOT(onChange()));

	_checkTimer.setSingleShot(true);
	connect(&_checkTimer, SIGNAL(timeout()), this, SLOT(onCheck()));

	connect(&_public, SIGNAL(changed()), this, SLOT(onPrivacyChange()));
	connect(&_private, SIGNAL(changed()), this, SLOT(onPrivacyChange()));

	prepare();
}

void SetupChannelBox::showAll() {
	_public.show();
	_private.show();
	if (_public.checked()) {
		_link.show();
	} else {
		_link.hide();
	}
	_save.show();
	_skip.show();
}

void SetupChannelBox::doSetInnerFocus() {
	if (_link.isHidden()) {
		setFocus();
	} else {
		_link.setFocus();
	}
}

void SetupChannelBox::updateMaxHeight() {
	if (!_channel->isMegagroup() || _public.checked()) {
		setMaxHeight(st::boxPadding.top() + st::newGroupPadding.top() + _public.height() + _aboutPublicHeight + st::newGroupSkip + _private.height() + _aboutPrivate.countHeight(_aboutPublicWidth) + st::newGroupSkip + st::newGroupPadding.bottom() + st::newGroupLinkPadding.top() + _link.height() + st::newGroupLinkPadding.bottom() + st::boxButtonPadding.top() + _save.height() + st::boxButtonPadding.bottom());
	} else {
		setMaxHeight(st::boxPadding.top() + st::newGroupPadding.top() + _public.height() + _aboutPublicHeight + st::newGroupSkip + _private.height() + _aboutPrivate.countHeight(_aboutPublicWidth) + st::newGroupSkip + st::newGroupPadding.bottom() + st::boxButtonPadding.top() + _save.height() + st::boxButtonPadding.bottom());
	}
}

void SetupChannelBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_link.hasFocus()) {
			if (_link.text().trimmed().isEmpty()) {
				_link.setFocus();
				_link.showError();
			} else {
				onSave();
			}
		}
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void SetupChannelBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	p.setPen(st::newGroupAboutFg);

	QRect aboutPublic(st::boxPadding.left() + st::newGroupPadding.left() + st::defaultRadiobutton.textPosition.x(), _public.y() + _public.height(), _aboutPublicWidth, _aboutPublicHeight);
	_aboutPublic.drawLeft(p, aboutPublic.x(), aboutPublic.y(), aboutPublic.width(), width());

	QRect aboutPrivate(st::boxPadding.left() + st::newGroupPadding.left() + st::defaultRadiobutton.textPosition.x(), _private.y() + _private.height(), _aboutPublicWidth, _aboutPublicHeight);
	_aboutPrivate.drawLeft(p, aboutPrivate.x(), aboutPrivate.y(), aboutPrivate.width(), width());

	if (!_channel->isMegagroup() || !_link.isHidden()) {
		p.setPen(st::black);
		p.setFont(st::newGroupLinkFont);
		p.drawTextLeft(st::boxPadding.left() + st::newGroupPadding.left() + st::defaultInputField.textMargins.left(), _link.y() - st::newGroupLinkPadding.top() + st::newGroupLinkTop, width(), lang(_link.isHidden() ? lng_create_group_invite_link : lng_create_group_link));
	}

	if (_link.isHidden()) {
		if (!_channel->isMegagroup()) {
			QTextOption option(style::al_left);
			option.setWrapMode(QTextOption::WrapAnywhere);
			p.setFont(_linkOver ? st::boxTextFont->underline() : st::boxTextFont);
			p.setPen(st::btnDefLink.color);
			p.drawText(_invitationLink, _channel->inviteLink(), option);
			if (!_goodTextLink.isEmpty() && a_goodOpacity.current() > 0) {
				p.setOpacity(a_goodOpacity.current());
				p.setPen(st::setGoodColor);
				p.setFont(st::boxTextFont);
				p.drawTextRight(st::boxPadding.right(), _link.y() - st::newGroupLinkPadding.top() + st::newGroupLinkTop + st::newGroupLinkFont->ascent - st::boxTextFont->ascent, width(), _goodTextLink);
				p.setOpacity(1);
			}
		}
	} else {
		if (!_errorText.isEmpty()) {
			p.setPen(st::setErrColor);
			p.setFont(st::boxTextFont);
			p.drawTextRight(st::boxPadding.right(), _link.y() - st::newGroupLinkPadding.top() + st::newGroupLinkTop + st::newGroupLinkFont->ascent - st::boxTextFont->ascent, width(), _errorText);
		} else if (!_goodText.isEmpty()) {
			p.setPen(st::setGoodColor);
			p.setFont(st::boxTextFont);
			p.drawTextRight(st::boxPadding.right(), _link.y() - st::newGroupLinkPadding.top() + st::newGroupLinkTop + st::newGroupLinkFont->ascent - st::boxTextFont->ascent, width(), _goodText);
		}
	}
}

void SetupChannelBox::resizeEvent(QResizeEvent *e) {
	_public.moveToLeft(st::boxPadding.left() + st::newGroupPadding.left(), st::boxPadding.top() + st::newGroupPadding.top());
	_private.moveToLeft(st::boxPadding.left() + st::newGroupPadding.left(), _public.y() + _public.height() + _aboutPublicHeight + st::newGroupSkip);

	_link.resize(width() - st::boxPadding.left() - st::newGroupLinkPadding.left() - st::boxPadding.right(), _link.height());
	_link.moveToLeft(st::boxPadding.left() + st::newGroupLinkPadding.left(), _private.y() + _private.height() + _aboutPrivate.countHeight(_aboutPublicWidth) + st::newGroupSkip + st::newGroupPadding.bottom() + st::newGroupLinkPadding.top());
	_invitationLink = QRect(_link.x(), _link.y() + (_link.height() / 2) - st::boxTextFont->height, _link.width(), 2 * st::boxTextFont->height);

	_save.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _save.height());
	_skip.moveToRight(st::boxButtonPadding.right() + _save.width() + st::boxButtonPadding.left(), _save.y());
	AbstractBox::resizeEvent(e);
}

void SetupChannelBox::mouseMoveEvent(QMouseEvent *e) {
	updateSelected(e->globalPos());
}

void SetupChannelBox::mousePressEvent(QMouseEvent *e) {
	if (_linkOver) {
		Application::clipboard()->setText(_channel->inviteLink());
		_goodTextLink = lang(lng_create_channel_link_copied);
		a_goodOpacity = anim::fvalue(1, 0);
		_a_goodFade.start();
	}
}

void SetupChannelBox::leaveEvent(QEvent *e) {
	updateSelected(QCursor::pos());
}

void SetupChannelBox::updateSelected(const QPoint &cursorGlobalPosition) {
	QPoint p(mapFromGlobal(cursorGlobalPosition));

	bool linkOver = _invitationLink.contains(p);
	if (linkOver != _linkOver) {
		_linkOver = linkOver;
		update();
		setCursor(_linkOver ? style::cur_pointer : style::cur_default);
	}
}

void SetupChannelBox::step_goodFade(float64 ms, bool timer) {
	float dt = ms / st::newGroupLinkFadeDuration;
	if (dt >= 1) {
		_a_goodFade.stop();
		a_goodOpacity.finish();
	} else {
		a_goodOpacity.update(dt, anim::linear);
	}
	if (timer) update();
}

void SetupChannelBox::closePressed() {
	if (!_existing) {
		Ui::showLayer(new ContactsBox(_channel));
	}
}

void SetupChannelBox::onSave() {
	if (!_public.checked()) {
		if (_existing) {
			_sentUsername = QString();
			_saveRequestId = MTP::send(MTPchannels_UpdateUsername(_channel->inputChannel, MTP_string(_sentUsername)), rpcDone(&SetupChannelBox::onUpdateDone), rpcFail(&SetupChannelBox::onUpdateFail));
		} else {
			onClose();
		}
	}

	if (_saveRequestId) return;

	QString link = _link.text().trimmed();
	if (link.isEmpty()) {
		_link.setFocus();
		_link.showError();
		return;
	}

	_sentUsername = link;
	_saveRequestId = MTP::send(MTPchannels_UpdateUsername(_channel->inputChannel, MTP_string(_sentUsername)), rpcDone(&SetupChannelBox::onUpdateDone), rpcFail(&SetupChannelBox::onUpdateFail));
}

void SetupChannelBox::onChange() {
	QString name = _link.text().trimmed();
	if (name.isEmpty()) {
		if (!_errorText.isEmpty() || !_goodText.isEmpty()) {
			_errorText = _goodText = QString();
			update();
		}
		_checkTimer.stop();
	} else {
		int32 len = name.size();
		for (int32 i = 0; i < len; ++i) {
			QChar ch = name.at(i);
			if ((ch < 'A' || ch > 'Z') && (ch < 'a' || ch > 'z') && (ch < '0' || ch > '9') && ch != '_') {
				if (_errorText != lang(lng_create_channel_link_bad_symbols)) {
					_errorText = lang(lng_create_channel_link_bad_symbols);
					update();
				}
				_checkTimer.stop();
				return;
			}
		}
		if (name.size() < MinUsernameLength) {
			if (_errorText != lang(lng_create_channel_link_too_short)) {
				_errorText = lang(lng_create_channel_link_too_short);
				update();
			}
			_checkTimer.stop();
		} else {
			if (!_errorText.isEmpty() || !_goodText.isEmpty()) {
				_errorText = _goodText = QString();
				update();
			}
			_checkTimer.start(UsernameCheckTimeout);
		}
	}
}

void SetupChannelBox::onCheck() {
	if (_checkRequestId) {
		MTP::cancel(_checkRequestId);
	}
	QString link = _link.text().trimmed();
	if (link.size() >= MinUsernameLength) {
		_checkUsername = link;
		_checkRequestId = MTP::send(MTPchannels_CheckUsername(_channel->inputChannel, MTP_string(link)), rpcDone(&SetupChannelBox::onCheckDone), rpcFail(&SetupChannelBox::onCheckFail));
	}
}

void SetupChannelBox::onPrivacyChange() {
	if (_public.checked()) {
		if (_tooMuchUsernames) {
			_private.setChecked(true);
			Ui::showLayer(new RevokePublicLinkBox([this, weak_this = weakThis()]() {
				if (!weak_this) return;
				_tooMuchUsernames = false;
				_public.setChecked(true);
				onCheck();
			}), KeepOtherLayers);
			return;
		}
		_link.show();
		_link.setFocus();
	} else {
		_link.hide();
		setFocus();
	}
	if (_channel->isMegagroup()) {
		updateMaxHeight();
	}
	update();
}

void SetupChannelBox::onUpdateDone(const MTPBool &result) {
	_channel->setName(textOneLine(_channel->name), _sentUsername);
	onClose();
}

bool SetupChannelBox::onUpdateFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_saveRequestId = 0;
	QString err(error.type());
	if (err == "USERNAME_NOT_MODIFIED" || _sentUsername == _channel->username) {
		_channel->setName(textOneLine(_channel->name), textOneLine(_sentUsername));
		onClose();
		return true;
	} else if (err == "USERNAME_INVALID") {
		_link.setFocus();
		_link.showError();
		_errorText = lang(lng_create_channel_link_invalid);
		update();
		return true;
	} else if (err == "USERNAME_OCCUPIED" || err == "USERNAMES_UNAVAILABLE") {
		_link.setFocus();
		_link.showError();
		_errorText = lang(lng_create_channel_link_occupied);
		update();
		return true;
	}
	_link.setFocus();
	return true;
}

void SetupChannelBox::onCheckDone(const MTPBool &result) {
	_checkRequestId = 0;
	QString newError = (mtpIsTrue(result) || _checkUsername == _channel->username) ? QString() : lang(lng_create_channel_link_occupied);
	QString newGood = newError.isEmpty() ? lang(lng_create_channel_link_available) : QString();
	if (_errorText != newError || _goodText != newGood) {
		_errorText = newError;
		_goodText = newGood;
		update();
	}
}

bool SetupChannelBox::onCheckFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_checkRequestId = 0;
	QString err(error.type());
	if (err == qstr("CHANNEL_PUBLIC_GROUP_NA")) {
		Ui::hideLayer();
		return true;
	} else if (err == qstr("CHANNELS_ADMIN_PUBLIC_TOO_MUCH")) {
		if (_existing) {
			showRevokePublicLinkBoxForEdit();
		} else {
			_tooMuchUsernames = true;
			_private.setChecked(true);
			onPrivacyChange();
		}
		return true;
	} else if (err == qstr("USERNAME_INVALID")) {
		_errorText = lang(lng_create_channel_link_invalid);
		update();
		return true;
	} else if (err == qstr("USERNAME_OCCUPIED") && _checkUsername != _channel->username) {
		_errorText = lang(lng_create_channel_link_occupied);
		update();
		return true;
	}
	_goodText = QString();
	_link.setFocus();
	return true;
}

void SetupChannelBox::showRevokePublicLinkBoxForEdit() {
	onClose();
	Ui::showLayer(new RevokePublicLinkBox([channel = _channel, existing = _existing]() {
		Ui::showLayer(new SetupChannelBox(channel, existing), KeepOtherLayers);
	}), KeepOtherLayers);
}

bool SetupChannelBox::onFirstCheckFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_checkRequestId = 0;
	QString err(error.type());
	if (err == qstr("CHANNEL_PUBLIC_GROUP_NA")) {
		Ui::hideLayer();
		return true;
	} else if (err == qstr("CHANNELS_ADMIN_PUBLIC_TOO_MUCH")) {
		if (_existing) {
			showRevokePublicLinkBoxForEdit();
		} else {
			_tooMuchUsernames = true;
			_private.setChecked(true);
			onPrivacyChange();
		}
		return true;
	}
	_goodText = QString();
	_link.setFocus();
	return true;
}

EditNameTitleBox::EditNameTitleBox(PeerData *peer) :
_peer(peer),
_save(this, lang(lng_settings_save), st::defaultBoxButton),
_cancel(this, lang(lng_cancel), st::cancelBoxButton),
_first(this, st::defaultInputField, lang(peer->isUser() ? lng_signup_firstname : lng_dlg_new_group_name), peer->isUser() ? peer->asUser()->firstName : peer->name),
_last(this, st::defaultInputField, lang(lng_signup_lastname), peer->isUser() ? peer->asUser()->lastName : QString()),
_invertOrder(!peer->isChat() && langFirstNameGoesSecond()),
_requestId(0) {
	if (_invertOrder) {
		setTabOrder(&_last, &_first);
	}
	_first.setMaxLength(MaxGroupChannelTitle);
	_last.setMaxLength(MaxGroupChannelTitle);

	int32 h = st::boxTitleHeight + st::contactPadding.top() + _first.height();
	if (_peer->isUser()) {
		_boxTitle = lang(_peer == App::self() ? lng_edit_self_title : lng_edit_contact_title);
		h += st::contactSkip + _last.height();
	} else if (_peer->isChat()) {
		_boxTitle = lang(lng_edit_group_title);
	}
	h += st::boxPadding.bottom() + st::contactPadding.bottom() + st::boxButtonPadding.top() + _save.height() + st::boxButtonPadding.bottom();
	setMaxHeight(h);

	connect(&_save, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(&_first, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));
	connect(&_last, SIGNAL(submitted(bool)), this, SLOT(onSubmit()));

	prepare();
}

void EditNameTitleBox::showAll() {
	_first.show();
	if (_peer->isChat()) {
		_last.hide();
	} else {
		_last.show();
	}
	_save.show();
	_cancel.show();
}

void EditNameTitleBox::doSetInnerFocus() {
	(_invertOrder ? _last : _first).setFocus();
}

void EditNameTitleBox::onSubmit() {
	if (_first.hasFocus()) {
		if (_peer->isChat()) {
			if (_first.getLastText().trimmed().isEmpty()) {
				_first.setFocus();
				_first.showError();
			} else {
				onSave();
			}
		} else {
			_last.setFocus();
		}
	} else if (_last.hasFocus()) {
		if (_first.getLastText().trimmed().isEmpty()) {
			_first.setFocus();
			_first.showError();
		} else if (_last.getLastText().trimmed().isEmpty()) {
			_last.setFocus();
			_last.showError();
		} else {
			onSave();
		}
	}
}

void EditNameTitleBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, _boxTitle);
}

void EditNameTitleBox::resizeEvent(QResizeEvent *e) {
	_first.resize(width() - st::boxPadding.left() - st::newGroupInfoPadding.left() - st::boxPadding.right(), _first.height());
	_last.resize(_first.size());
	if (_invertOrder) {
		_last.moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), st::boxTitleHeight + st::contactPadding.top());
		_first.moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), _last.y() + _last.height() + st::contactSkip);
	} else {
		_first.moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), st::boxTitleHeight + st::contactPadding.top());
		_last.moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), _first.y() + _first.height() + st::contactSkip);
	}

	_save.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _save.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _save.width() + st::boxButtonPadding.left(), _save.y());
	AbstractBox::resizeEvent(e);
}

void EditNameTitleBox::onSave() {
	if (_requestId) return;

	QString first = prepareText(_first.getLastText()), last = prepareText(_last.getLastText());
	if (first.isEmpty() && last.isEmpty()) {
		if (_invertOrder) {
			_last.setFocus();
			_last.showError();
		} else {
			_first.setFocus();
			_first.showError();
		}
		return;
	}
	if (first.isEmpty()) {
		first = last;
		last = QString();
	}
	_sentName = first;
	if (_peer == App::self()) {
		MTPaccount_UpdateProfile::Flags flags = MTPaccount_UpdateProfile::Flag::f_first_name | MTPaccount_UpdateProfile::Flag::f_last_name;
		_requestId = MTP::send(MTPaccount_UpdateProfile(MTP_flags(flags), MTP_string(first), MTP_string(last), MTPstring()), rpcDone(&EditNameTitleBox::onSaveSelfDone), rpcFail(&EditNameTitleBox::onSaveSelfFail));
	} else if (_peer->isChat()) {
		_requestId = MTP::send(MTPmessages_EditChatTitle(_peer->asChat()->inputChat, MTP_string(first)), rpcDone(&EditNameTitleBox::onSaveChatDone), rpcFail(&EditNameTitleBox::onSaveChatFail));
	}
}

void EditNameTitleBox::onSaveSelfDone(const MTPUser &user) {
	App::feedUsers(MTP_vector<MTPUser>(1, user));
	onClose();
}

bool EditNameTitleBox::onSaveSelfFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	QString err(error.type());
	QString first = textOneLine(_first.getLastText().trimmed()), last = textOneLine(_last.getLastText().trimmed());
	if (err == "NAME_NOT_MODIFIED") {
		App::self()->setName(first, last, QString(), textOneLine(App::self()->username));
		onClose();
		return true;
	} else if (err == "FIRSTNAME_INVALID") {
		_first.setFocus();
		_first.showError();
		return true;
	} else if (err == "LASTNAME_INVALID") {
		_last.setFocus();
		_last.showError();
		return true;
	}
	_first.setFocus();
	return true;
}

bool EditNameTitleBox::onSaveChatFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) return false;

	_requestId = 0;
	QString err(error.type());
	if (err == qstr("CHAT_TITLE_NOT_MODIFIED") || err == qstr("CHAT_NOT_MODIFIED")) {
		if (auto chatData = _peer->asChat()) {
			chatData->setName(_sentName);
		}
		onClose();
		return true;
	} else if (err == qstr("NO_CHAT_TITLE")) {
		_first.setFocus();
		_first.showError();
		return true;
	}
	_first.setFocus();
	return true;
}

void EditNameTitleBox::onSaveChatDone(const MTPUpdates &updates) {
	App::main()->sentUpdatesReceived(updates);
	onClose();
}

EditChannelBox::EditChannelBox(ChannelData *channel) : AbstractBox()
, _channel(channel)
, _save(this, lang(lng_settings_save), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _title(this, st::defaultInputField, lang(lng_dlg_new_channel_name), _channel->name)
, _description(this, st::newGroupDescription, lang(lng_create_group_description), _channel->about())
, _sign(this, lang(lng_edit_sign_messages), channel->addsSignature(), st::defaultBoxCheckbox)
, _publicLink(this, lang(channel->isPublic() ? lng_profile_edit_public_link : lng_profile_create_public_link), st::defaultBoxLinkButton)
, _saveTitleRequestId(0)
, _saveDescriptionRequestId(0)
, _saveSignRequestId(0) {
	connect(App::main(), SIGNAL(peerNameChanged(PeerData*,const PeerData::Names&,const PeerData::NameFirstChars&)), this, SLOT(peerUpdated(PeerData*)));

	setMouseTracking(true);

	_title.setMaxLength(MaxGroupChannelTitle);
	_description.setMaxLength(MaxChannelDescription);
	_description.resize(width() - st::boxPadding.left() - st::newGroupInfoPadding.left() - st::boxPadding.right(), _description.height());
	myEnsureResized(&_description);

	updateMaxHeight();
	connect(&_description, SIGNAL(resized()), this, SLOT(onDescriptionResized()));
	connect(&_description, SIGNAL(submitted(bool)), this, SLOT(onSave()));
	connect(&_description, SIGNAL(cancelled()), this, SLOT(onClose()));

	connect(&_save, SIGNAL(clicked()), this, SLOT(onSave()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(onClose()));

	connect(&_publicLink, SIGNAL(clicked()), this, SLOT(onPublicLink()));

	prepare();
}

void EditChannelBox::showAll() {
	_title.show();
	_description.show();
	_save.show();
	_cancel.show();
	if (_channel->canEditUsername()) {
		_publicLink.show();
	} else {
		_publicLink.hide();
	}
	if (_channel->isMegagroup()) {
		_sign.hide();
	} else {
		_sign.show();
	}
}

void EditChannelBox::doSetInnerFocus() {
	_title.setFocus();
}

void EditChannelBox::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (_title.hasFocus()) {
			onSave();
		}
	} else {
		AbstractBox::keyPressEvent(e);
	}
}

void EditChannelBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	paintTitle(p, lang(_channel->isMegagroup() ? lng_edit_group : lng_edit_channel_title));
}

void EditChannelBox::peerUpdated(PeerData *peer) {
	if (peer == _channel) {
		_publicLink.setText(lang(_channel->isPublic() ? lng_profile_edit_public_link : lng_profile_create_public_link));
		_sign.setChecked(_channel->addsSignature());
	}
}

void EditChannelBox::onDescriptionResized() {
	updateMaxHeight();
	update();
}

void EditChannelBox::updateMaxHeight() {
	int32 h = st::boxTitleHeight + st::newGroupInfoPadding.top() + _title.height();
	h += st::newGroupDescriptionPadding.top() + _description.height() + st::newGroupDescriptionPadding.bottom();
	if (!_channel->isMegagroup()) {
		h += st::newGroupPublicLinkPadding.top() + _sign.height() + st::newGroupPublicLinkPadding.bottom();
	}
	if (_channel->canEditUsername()) {
		h += st::newGroupPublicLinkPadding.top() + _publicLink.height() + st::newGroupPublicLinkPadding.bottom();
	}
	h += st::boxPadding.bottom() + st::newGroupInfoPadding.bottom() + st::boxButtonPadding.top() + _save.height() + st::boxButtonPadding.bottom();
	setMaxHeight(h);
}

void EditChannelBox::resizeEvent(QResizeEvent *e) {
	_title.resize(width() - st::boxPadding.left() - st::newGroupInfoPadding.left() - st::boxPadding.right(), _title.height());
	_title.moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), st::boxTitleHeight + st::newGroupInfoPadding.top() + st::newGroupNamePosition.y());

	_description.moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), _title.y() + _title.height() + st::newGroupDescriptionPadding.top());

	_sign.moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), _description.y() + _description.height() + st::newGroupDescriptionPadding.bottom() + st::newGroupPublicLinkPadding.top());

	if (_channel->isMegagroup()) {
		_publicLink.moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), _description.y() + _description.height() + st::newGroupDescriptionPadding.bottom() + st::newGroupPublicLinkPadding.top());
	} else {
		_publicLink.moveToLeft(st::boxPadding.left() + st::newGroupInfoPadding.left(), _sign.y() + _sign.height() + st::newGroupDescriptionPadding.bottom() + st::newGroupPublicLinkPadding.top());
	}

	_save.moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _save.height());
	_cancel.moveToRight(st::boxButtonPadding.right() + _save.width() + st::boxButtonPadding.left(), _save.y());
	AbstractBox::resizeEvent(e);
}

void EditChannelBox::onSave() {
	if (_saveTitleRequestId || _saveDescriptionRequestId || _saveSignRequestId) return;

	QString title = prepareText(_title.getLastText()), description = prepareText(_description.getLastText(), true);
	if (title.isEmpty()) {
		_title.setFocus();
		_title.showError();
		return;
	}
	_sentTitle = title;
	_sentDescription = description;
	if (_sentTitle == _channel->name) {
		saveDescription();
	} else {
		_saveTitleRequestId = MTP::send(MTPchannels_EditTitle(_channel->inputChannel, MTP_string(_sentTitle)), rpcDone(&EditChannelBox::onSaveTitleDone), rpcFail(&EditChannelBox::onSaveFail));
	}
}

void EditChannelBox::onPublicLink() {
	Ui::showLayer(new SetupChannelBox(_channel, true), KeepOtherLayers);
}

void EditChannelBox::saveDescription() {
	if (_sentDescription == _channel->about()) {
		saveSign();
	} else {
		_saveDescriptionRequestId = MTP::send(MTPchannels_EditAbout(_channel->inputChannel, MTP_string(_sentDescription)), rpcDone(&EditChannelBox::onSaveDescriptionDone), rpcFail(&EditChannelBox::onSaveFail));
	}
}

void EditChannelBox::saveSign() {
	if (_channel->isMegagroup() || _channel->addsSignature() == _sign.checked()) {
		onClose();
	} else {
		_saveSignRequestId = MTP::send(MTPchannels_ToggleSignatures(_channel->inputChannel, MTP_bool(_sign.checked())), rpcDone(&EditChannelBox::onSaveSignDone), rpcFail(&EditChannelBox::onSaveFail));
	}
}

bool EditChannelBox::onSaveFail(const RPCError &error, mtpRequestId req) {
	if (MTP::isDefaultHandledError(error)) return false;

	QString err(error.type());
	if (req == _saveTitleRequestId) {
		_saveTitleRequestId = 0;
		if (err == qstr("CHAT_NOT_MODIFIED") || err == qstr("CHAT_TITLE_NOT_MODIFIED")) {
			_channel->setName(_sentTitle, _channel->username);
			saveDescription();
			return true;
		} else if (err == qstr("NO_CHAT_TITLE")) {
			_title.setFocus();
			_title.showError();
			return true;
		} else {
			_title.setFocus();
		}
	} else if (req == _saveDescriptionRequestId) {
		_saveDescriptionRequestId = 0;
		if (err == qstr("CHAT_ABOUT_NOT_MODIFIED")) {
			if (_channel->setAbout(_sentDescription)) {
				if (App::api()) {
					emit App::api()->fullPeerUpdated(_channel);
				}
			}
			saveSign();
			return true;
		} else {
			_description.setFocus();
		}
	} else if (req == _saveSignRequestId) {
		_saveSignRequestId = 0;
		if (err == qstr("CHAT_NOT_MODIFIED")) {
			onClose();
			return true;
		}
	}
	return true;
}

void EditChannelBox::onSaveTitleDone(const MTPUpdates &updates) {
	_saveTitleRequestId = 0;
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
	saveDescription();
}

void EditChannelBox::onSaveDescriptionDone(const MTPBool &result) {
	_saveDescriptionRequestId = 0;
	if (_channel->setAbout(_sentDescription)) {
		if (App::api()) {
			emit App::api()->fullPeerUpdated(_channel);
		}
	}
	saveSign();
}

void EditChannelBox::onSaveSignDone(const MTPUpdates &updates) {
	_saveSignRequestId = 0;
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
	onClose();
}

RevokePublicLinkBox::RevokePublicLinkBox(base::lambda_unique<void()> &&revokeCallback) : AbstractBox()
, _rowHeight(st::contactsPadding.top() + st::contactsPhotoSize + st::contactsPadding.bottom())
, _revokeWidth(st::normalFont->width(lang(lng_channels_too_much_public_revoke)))
, _aboutRevoke(this, lang(lng_channels_too_much_public_about), FlatLabel::InitType::Simple, st::aboutRevokePublicLabel)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, _revokeCallback(std_::move(revokeCallback)) {
	setMouseTracking(true);

	MTP::send(MTPchannels_GetAdminedPublicChannels(), rpcDone(&RevokePublicLinkBox::getPublicDone), rpcFail(&RevokePublicLinkBox::getPublicFail));

	updateMaxHeight();

	connect(_cancel, SIGNAL(clicked()), this, SLOT(onClose()));
	subscribe(FileDownload::ImageLoaded(), [this] { update(); });

	prepare();
}

void RevokePublicLinkBox::updateMaxHeight() {
	_rowsTop = st::boxPadding.top() + _aboutRevoke->height() + st::boxPadding.top();
	setMaxHeight(_rowsTop + (5 * _rowHeight) + st::boxButtonPadding.top() + _cancel->height() + st::boxButtonPadding.bottom());
}

void RevokePublicLinkBox::mouseMoveEvent(QMouseEvent *e) {
	updateSelected();
}

void RevokePublicLinkBox::updateSelected() {
	auto point = mapFromGlobal(QCursor::pos());
	PeerData *selected = nullptr;
	auto top = _rowsTop;
	for_const (auto &row, _rows) {
		auto revokeLink = rtlrect(width() - st::contactsPadding.right() - st::contactsCheckPosition.x() - _revokeWidth, top + st::contactsPadding.top() + (st::contactsPhotoSize - st::normalFont->height) / 2, _revokeWidth, st::normalFont->height, width());
		if (revokeLink.contains(point)) {
			selected = row.peer;
			break;
		}
		top += _rowHeight;
	}
	if (selected != _selected) {
		_selected = selected;
		setCursor((_selected || _pressed) ? style::cur_pointer : style::cur_default);
		update();
	}
}

void RevokePublicLinkBox::mousePressEvent(QMouseEvent *e) {
	if (_pressed != _selected) {
		_pressed = _selected;
		update();
	}
}

void RevokePublicLinkBox::mouseReleaseEvent(QMouseEvent *e) {
	auto pressed = base::take(_pressed);
	setCursor((_selected || _pressed) ? style::cur_pointer : style::cur_default);
	if (pressed && pressed == _selected) {
		auto text_method = pressed->isMegagroup() ? lng_channels_too_much_public_revoke_confirm_group : lng_channels_too_much_public_revoke_confirm_channel;
		auto text = text_method(lt_link, qsl("telegram.me/") + pressed->userName(), lt_group, pressed->name);
		weakRevokeConfirmBox = new ConfirmBox(text, lang(lng_channels_too_much_public_revoke));
		struct Data {
			Data(QPointer<TWidget> &&weakThis, PeerData *pressed) : weakThis(std_::move(weakThis)), pressed(pressed) {
			}
			QPointer<TWidget> weakThis;
			PeerData *pressed;
		};
		weakRevokeConfirmBox->setConfirmedCallback([this, data = std_::make_unique<Data>(weakThis(), pressed)]() {
			if (!data->weakThis) return;
			if (_revokeRequestId) return;
			_revokeRequestId = MTP::send(MTPchannels_UpdateUsername(data->pressed->asChannel()->inputChannel, MTP_string("")), rpcDone(&RevokePublicLinkBox::revokeLinkDone), rpcFail(&RevokePublicLinkBox::revokeLinkFail));
		});
		Ui::showLayer(weakRevokeConfirmBox, KeepOtherLayers);
	}
}

void RevokePublicLinkBox::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (paint(p)) return;

	p.translate(0, _rowsTop);
	for_const (auto &row, _rows) {
		paintChat(p, row, (row.peer == _selected), (row.peer == _pressed));
		p.translate(0, _rowHeight);
	}
}

void RevokePublicLinkBox::resizeEvent(QResizeEvent *e) {
	_aboutRevoke->moveToLeft(st::boxPadding.left(), st::boxPadding.top());
	_cancel->moveToRight(st::boxButtonPadding.right(), height() - st::boxButtonPadding.bottom() - _cancel->height());
	AbstractBox::resizeEvent(e);
}

void RevokePublicLinkBox::paintChat(Painter &p, const ChatRow &row, bool selected, bool pressed) const {
	auto peer = row.peer;
	peer->paintUserpicLeft(p, st::contactsPhotoSize, st::contactsPadding.left(), st::contactsPadding.top(), width());

	p.setPen(st::black);

	int32 namex = st::contactsPadding.left() + st::contactsPhotoSize + st::contactsPadding.left();
	int32 namew = width() - namex - st::contactsPadding.right() - (_revokeWidth + st::contactsCheckPosition.x() * 2);
	if (peer->isVerified()) {
		auto icon = &st::dialogsVerifiedIcon;
		namew -= icon->width();
		icon->paint(p, namex + qMin(row.name.maxWidth(), namew), st::contactsPadding.top() + st::contactsNameTop, width());
	}
	row.name.drawLeftElided(p, namex, st::contactsPadding.top() + st::contactsNameTop, namew, width());

	p.setFont(selected ? st::linkOverFont : st::linkFont);
	p.setPen(pressed ? st::btnDefLink.downColor : st::btnDefLink.color->p);
	p.drawTextRight(st::contactsPadding.right() + st::contactsCheckPosition.x(), st::contactsPadding.top() + (st::contactsPhotoSize - st::normalFont->height) / 2, width(), lang(lng_channels_too_much_public_revoke), _revokeWidth);

	p.setPen(st::contactsStatusFg);
	textstyleSet(&st::revokePublicLinkStatusStyle);
	row.status.drawLeftElided(p, namex, st::contactsPadding.top() + st::contactsStatusTop, namew, width());
	textstyleRestore();
}

void RevokePublicLinkBox::getPublicDone(const MTPmessages_Chats &result) {
	if (result.type() == mtpc_messages_chats) {
		auto &chats = result.c_messages_chats().vchats;
		for_const (auto &chat, chats.c_vector().v) {
			if (auto peer = App::feedChat(chat)) {
				if (!peer->isChannel() || peer->userName().isEmpty()) continue;

				ChatRow row;
				row.peer = peer;
				row.name.setText(st::contactsNameFont, peer->name, _textNameOptions);
				textstyleSet(&st::revokePublicLinkStatusStyle);
				row.status.setText(st::normalFont, qsl("telegram.me/") + textcmdLink(1, peer->userName()), _textDlgOptions);
				textstyleRestore();
				_rows.push_back(std_::move(row));
			}
		}
	}
	update();
}

bool RevokePublicLinkBox::getPublicFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) {
		return false;
	}

	return true;
}

void RevokePublicLinkBox::revokeLinkDone(const MTPBool &result) {
	if (weakRevokeConfirmBox) {
		weakRevokeConfirmBox->onClose();
	}
	onClose();
	if (_revokeCallback) {
		_revokeCallback();
	}
}

bool RevokePublicLinkBox::revokeLinkFail(const RPCError &error) {
	if (MTP::isDefaultHandledError(error)) {
		return false;
	}

	return true;
}
