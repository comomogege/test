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
#include "intro/introsignup.h"

#include "ui/filedialog.h"
#include "boxes/photocropbox.h"
#include "lang.h"
#include "application.h"

IntroSignup::IntroSignup(IntroWidget *parent) : IntroStep(parent)
, a_errorAlpha(0)
, a_photoOver(0)
, _a_error(animation(this, &IntroSignup::step_error))
, _a_photo(animation(this, &IntroSignup::step_photo))
, next(this, lang(lng_intro_finish), st::btnIntroNext)
, first(this, st::inpIntroName, lang(lng_signup_firstname))
, last(this, st::inpIntroName, lang(lng_signup_lastname))
, sentRequest(0)
, _invertOrder(langFirstNameGoesSecond()) {
	setVisible(false);
	setGeometry(parent->innerRect());

	connect(&next, SIGNAL(clicked()), this, SLOT(onSubmitName()));
	connect(&checkRequest, SIGNAL(timeout()), this, SLOT(onCheckRequest()));

	if (_invertOrder) {
		setTabOrder(&last, &first);
	}

	setMouseTracking(true);
}

void IntroSignup::mouseMoveEvent(QMouseEvent *e) {
	bool photoOver = QRect(_phLeft, _phTop, st::introPhotoSize, st::introPhotoSize).contains(e->pos());
	if (photoOver != _photoOver) {
		_photoOver = photoOver;
		if (_photoSmall.isNull()) {
			a_photoOver.start(_photoOver ? 1 : 0);
			_a_photo.start();
		}
	}

	setCursor(_photoOver ? style::cur_pointer : style::cur_default);
}

void IntroSignup::mousePressEvent(QMouseEvent *e) {
	mouseMoveEvent(e);
	if (QRect(_phLeft, _phTop, st::introPhotoSize, st::introPhotoSize).contains(e->pos())) {
		QStringList imgExtensions(cImgExtensions());
		QString filter(qsl("Image files (*") + imgExtensions.join(qsl(" *")) + qsl(");;") + filedialogAllFilesFilter());

		QImage img;
		QString file;
		QByteArray remoteContent;
		if (filedialogGetOpenFile(file, remoteContent, lang(lng_choose_images), filter)) {
			if (!remoteContent.isEmpty()) {
				img = App::readImage(remoteContent);
			} else {
				if (!file.isEmpty()) {
					img = App::readImage(file);
				}
			}
		} else {
			return;
		}

		if (img.isNull() || img.width() > 10 * img.height() || img.height() > 10 * img.width()) {
			showError(lang(lng_bad_photo));
			return;
		}
		PhotoCropBox *box = new PhotoCropBox(img, PeerId(0));
		connect(box, SIGNAL(ready(const QImage &)), this, SLOT(onPhotoReady(const QImage &)));
		Ui::showLayer(box);
	}
}

void IntroSignup::paintEvent(QPaintEvent *e) {
	bool trivial = (rect() == e->rect());

	Painter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	if (trivial || e->rect().intersects(textRect)) {
		p.setFont(st::introHeaderFont->f);
		p.drawText(textRect, lang(lng_signup_title), style::al_top);
		p.setFont(st::introFont->f);
		p.drawText(textRect, lang(lng_signup_desc), style::al_bottom);
	}
	if (_a_error.animating() || error.length()) {
		p.setOpacity(a_errorAlpha.current());

		QRect errRect;
		if (_invertOrder) {
			errRect = QRect((width() - st::introErrWidth) / 2, (first.y() + first.height() + next.y() - st::introErrHeight) / 2, st::introErrWidth, st::introErrHeight);
		} else {
			errRect = QRect((width() - st::introErrWidth) / 2, (last.y() + last.height() + next.y() - st::introErrHeight) / 2, st::introErrWidth, st::introErrHeight);
		}
		p.setFont(st::introErrFont->f);
		p.setPen(st::introErrColor->p);
		p.drawText(errRect, error, QTextOption(style::al_center));

		p.setOpacity(1);
	}

	if (_photoSmall.isNull()) {
		float64 o = a_photoOver.current();
		QRect phRect(_phLeft, _phTop, st::introPhotoSize, st::introPhotoSize);
		if (o > 0) {
			if (o < 1) {
				QColor c;
				c.setRedF(st::newGroupPhotoBg->c.redF() * (1. - o) + st::newGroupPhotoBgOver->c.redF() * o);
				c.setGreenF(st::newGroupPhotoBg->c.greenF() * (1. - o) + st::newGroupPhotoBgOver->c.greenF() * o);
				c.setBlueF(st::newGroupPhotoBg->c.blueF() * (1. - o) + st::newGroupPhotoBgOver->c.blueF() * o);
				p.fillRect(phRect, c);
			} else {
				p.fillRect(phRect, st::newGroupPhotoBgOver);
			}
		} else {
			p.fillRect(phRect, st::newGroupPhotoBg);
		}
		p.drawSpriteCenter(phRect, st::newGroupPhotoIcon);
	} else {
		p.drawPixmap(_phLeft, _phTop, _photoSmall);
	}
}

void IntroSignup::resizeEvent(QResizeEvent *e) {
	_phLeft = (width() - next.width()) / 2;
	_phTop = st::introTextTop + st::introTextSize.height() + st::introCountry.top;
	if (e->oldSize().width() != width()) {
		next.move((width() - next.width()) / 2, st::introBtnTop);
		if (_invertOrder) {
			last.move((width() - next.width()) / 2 + next.width() - last.width(), _phTop);
			first.move((width() - next.width()) / 2 + next.width() - first.width(), last.y() + st::introCountry.height + st::introCountry.ptrSize.height() + st::introPhoneTop);
		} else {
			first.move((width() - next.width()) / 2 + next.width() - first.width(), _phTop);
			last.move((width() - next.width()) / 2 + next.width() - last.width(), first.y() + st::introCountry.height + st::introCountry.ptrSize.height() + st::introPhoneTop);
		}
	}
	textRect = QRect((width() - st::introTextSize.width()) / 2, st::introTextTop, st::introTextSize.width(), st::introTextSize.height());
}

void IntroSignup::showError(const QString &err) {
	if (!_a_error.animating() && err == error) return;

	if (err.length()) {
		error = err;
		a_errorAlpha.start(1);
	} else {
		a_errorAlpha.start(0);
	}
	_a_error.start();
}

void IntroSignup::step_error(float64 ms, bool timer) {
	float64 dt = ms / st::introErrDuration;

	if (dt >= 1) {
		_a_error.stop();
		a_errorAlpha.finish();
		if (!a_errorAlpha.current()) {
			error.clear();
		}
	} else {
		a_errorAlpha.update(dt, st::introErrFunc);
	}
	if (timer) update();
}

void IntroSignup::step_photo(float64 ms, bool timer) {
	float64 dt = ms / st::introErrDuration;

	if (dt >= 1) {
		_a_photo.stop();
		a_photoOver.finish();
	} else {
		a_photoOver.update(dt, anim::linear);
	}
	if (timer) update();
}

void IntroSignup::activate() {
	IntroStep::activate();
	if (_invertOrder) {
		last.setFocus();
	} else {
		first.setFocus();
	}
}

void IntroSignup::cancelled() {
	if (sentRequest) {
		MTP::cancel(sentRequest);
		sentRequest = 0;
	}
}

void IntroSignup::stopCheck() {
	checkRequest.stop();
}

void IntroSignup::onCheckRequest() {
	int32 status = MTP::state(sentRequest);
	if (status < 0) {
		int32 leftms = -status;
		if (leftms >= 1000) {
			MTP::cancel(sentRequest);
			sentRequest = 0;
			if (!first.isEnabled()) {
				first.setDisabled(false);
				last.setDisabled(false);
				if (_invertOrder) {
					first.setFocus();
				} else {
					last.setFocus();
				}
			}
		}
	}
	if (!sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void IntroSignup::onPhotoReady(const QImage &img) {
	_photoBig = img;
	_photoSmall = App::pixmapFromImageInPlace(img.scaled(st::introPhotoSize * cIntRetinaFactor(), st::introPhotoSize * cIntRetinaFactor(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
	_photoSmall.setDevicePixelRatio(cRetinaFactor());
}

void IntroSignup::nameSubmitDone(const MTPauth_Authorization &result) {
	stopCheck();
	first.setDisabled(false);
	last.setDisabled(false);
	const auto &d(result.c_auth_authorization());
	if (d.vuser.type() != mtpc_user || !d.vuser.c_user().is_self()) { // wtf?
		showError(lang(lng_server_error));
		return;
	}
	intro()->finish(d.vuser, _photoBig);
}

bool IntroSignup::nameSubmitFail(const RPCError &error) {
	if (MTP::isFloodError(error)) {
		stopCheck();
		first.setDisabled(false);
		last.setDisabled(false);
		showError(lang(lng_flood_error));
		if (_invertOrder) {
			first.setFocus();
		} else {
			last.setFocus();
		}
		return true;
	}
	if (MTP::isDefaultHandledError(error)) return false;

	stopCheck();
	first.setDisabled(false);
	last.setDisabled(false);
	const QString &err = error.type();
	if (err == qstr("PHONE_NUMBER_INVALID") || err == qstr("PHONE_CODE_EXPIRED") ||
		err == qstr("PHONE_CODE_EMPTY") || err == qstr("PHONE_CODE_INVALID") ||
		err == qstr("PHONE_NUMBER_OCCUPIED")) {
		intro()->onBack();
		return true;
	} else if (err == "FIRSTNAME_INVALID") {
		showError(lang(lng_bad_name));
		first.setFocus();
		return true;
	} else if (err == "LASTNAME_INVALID") {
		showError(lang(lng_bad_name));
		last.setFocus();
		return true;
	}
	if (cDebug()) { // internal server error
		showError(err + ": " + error.description());
	} else {
		showError(lang(lng_server_error));
	}
	if (_invertOrder) {
		last.setFocus();
	} else {
		first.setFocus();
	}
	return false;
}

void IntroSignup::onInputChange() {
	showError(QString());
}

void IntroSignup::onSubmitName(bool force) {
	if (_invertOrder) {
		if ((last.hasFocus() || last.text().trimmed().length()) && !first.text().trimmed().length()) {
			first.setFocus();
			return;
		} else if (!last.text().trimmed().length()) {
			last.setFocus();
			return;
		}
	} else {
		if ((first.hasFocus() || first.text().trimmed().length()) && !last.text().trimmed().length()) {
			last.setFocus();
			return;
		} else if (!first.text().trimmed().length()) {
			first.setFocus();
			return;
		}
	}
	if (!force && !first.isEnabled()) return;

	first.setDisabled(true);
	last.setDisabled(true);
	setFocus();

	showError(QString());

	firstName = first.text().trimmed();
	lastName = last.text().trimmed();
	sentRequest = MTP::send(MTPauth_SignUp(MTP_string(intro()->getPhone()), MTP_string(intro()->getPhoneHash()), MTP_string(intro()->getCode()), MTP_string(firstName), MTP_string(lastName)), rpcDone(&IntroSignup::nameSubmitDone), rpcFail(&IntroSignup::nameSubmitFail));
}

void IntroSignup::onSubmit() {
	onSubmitName();
}
