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
#include "platform/linux/file_dialog_linux.h"

#include <private/qguiapplication_p.h>
#include "platform/linux/linux_libs.h"
#include "platform/linux/linux_gdk_helper.h"
#include "mainwindow.h"
#include "localstorage.h"

QStringList qt_make_filter_list(const QString &filter);

namespace Platform {
namespace FileDialog {

using Type = ::FileDialog::internal::Type;

bool Supported() {
	return Platform::internal::GdkHelperLoaded()
		&& (Libs::gtk_widget_hide_on_delete != nullptr)
		&& (Libs::gtk_clipboard_store != nullptr)
		&& (Libs::gtk_clipboard_get != nullptr)
		&& (Libs::gtk_widget_destroy != nullptr)
		&& (Libs::gtk_dialog_get_type != nullptr)
		&& (Libs::gtk_dialog_run != nullptr)
		&& (Libs::gtk_widget_realize != nullptr)
		&& (Libs::gdk_window_set_modal_hint != nullptr)
		&& (Libs::gtk_widget_show != nullptr)
		&& (Libs::gdk_window_focus != nullptr)
		&& (Libs::gtk_widget_hide != nullptr)
		&& (Libs::gtk_widget_hide_on_delete != nullptr)
		&& (Libs::gtk_file_chooser_dialog_new != nullptr)
		&& (Libs::gtk_file_chooser_get_type != nullptr)
		&& (Libs::gtk_file_chooser_set_current_folder != nullptr)
		&& (Libs::gtk_file_chooser_get_current_folder != nullptr)
		&& (Libs::gtk_file_chooser_set_current_name != nullptr)
		&& (Libs::gtk_file_chooser_select_filename != nullptr)
		&& (Libs::gtk_file_chooser_get_filenames != nullptr)
		&& (Libs::gtk_file_chooser_set_filter != nullptr)
		&& (Libs::gtk_file_chooser_get_filter != nullptr)
		&& (Libs::gtk_window_get_type != nullptr)
		&& (Libs::gtk_window_set_title != nullptr)
		&& (Libs::gtk_file_chooser_set_local_only != nullptr)
		&& (Libs::gtk_file_chooser_set_action != nullptr)
		&& (Libs::gtk_file_chooser_set_select_multiple != nullptr)
		&& (Libs::gtk_file_chooser_set_do_overwrite_confirmation != nullptr)
		&& (Libs::gtk_file_chooser_remove_filter != nullptr)
		&& (Libs::gtk_file_filter_set_name != nullptr)
		&& (Libs::gtk_file_filter_add_pattern != nullptr)
		&& (Libs::gtk_file_chooser_add_filter != nullptr)
		&& (Libs::gtk_file_filter_new != nullptr);
}

bool Get(QStringList &files, QByteArray &remoteContent, const QString &caption, const QString &filter, Type type, QString startFile) {
	auto parent = App::wnd() ? App::wnd()->filedialogParent() : nullptr;
	internal::GtkFileDialog dialog(parent, caption, QString(), filter);

	dialog.setModal(true);
	if (type == Type::ReadFile || type == Type::ReadFiles) {
		dialog.setFileMode((type == Type::ReadFiles) ? QFileDialog::ExistingFiles : QFileDialog::ExistingFile);
		dialog.setAcceptMode(QFileDialog::AcceptOpen);
	} else if (type == Type::ReadFolder) {
		dialog.setAcceptMode(QFileDialog::AcceptOpen);
		dialog.setFileMode(QFileDialog::Directory);
		dialog.setOption(QFileDialog::ShowDirsOnly);
	} else {
		dialog.setFileMode(QFileDialog::AnyFile);
		dialog.setAcceptMode(QFileDialog::AcceptSave);
	}
	if (startFile.isEmpty() || startFile.at(0) != '/') {
		startFile = cDialogLastPath() + '/' + startFile;
	}
	dialog.selectFile(startFile);

	int res = dialog.exec();

	QString path = dialog.directory().absolutePath();
	if (path != cDialogLastPath()) {
		cSetDialogLastPath(path);
		Local::writeUserSettings();
	}

	if (res == QDialog::Accepted) {
		if (type == Type::ReadFiles) {
			files = dialog.selectedFiles();
		} else {
			files = dialog.selectedFiles().mid(0, 1);
		}
		return true;
	}

	files = QStringList();
	remoteContent = QByteArray();
	return false;
}

namespace internal {

QGtkDialog::QGtkDialog(GtkWidget *gtkWidget) : gtkWidget(gtkWidget) {
	Libs::g_signal_connect_swapped_helper(Libs::g_object_cast(gtkWidget), "response", GCallback(onResponse), this);
	Libs::g_signal_connect_helper(Libs::g_object_cast(gtkWidget), "delete-event", GCallback(Libs::gtk_widget_hide_on_delete), NULL);
}

QGtkDialog::~QGtkDialog() {
	Libs::gtk_clipboard_store(Libs::gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
	Libs::gtk_widget_destroy(gtkWidget);
}

GtkDialog *QGtkDialog::gtkDialog() const {
	return Libs::gtk_dialog_cast(gtkWidget);
}

void QGtkDialog::exec() {
	if (auto w = App::wnd()) {
		w->onReActivate();
		QTimer::singleShot(200, w, SLOT(onReActivate()));
	}
	if (modality() == Qt::ApplicationModal) {
		// block input to the whole app, including other GTK dialogs
		Libs::gtk_dialog_run(gtkDialog());
	} else {
		// block input to the window, allow input to other GTK dialogs
		QEventLoop loop;
		connect(this, SIGNAL(accept()), &loop, SLOT(quit()));
		connect(this, SIGNAL(reject()), &loop, SLOT(quit()));
		loop.exec();
	}
}

void QGtkDialog::show(Qt::WindowFlags flags, Qt::WindowModality modality, QWindow *parent) {
	connect(parent, &QWindow::destroyed, this, &QGtkDialog::onParentWindowDestroyed,
			Qt::UniqueConnection);
	setParent(parent);
	setFlags(flags);
	setModality(modality);

	Libs::gtk_widget_realize(gtkWidget); // creates X window

	if (parent) {
		Platform::internal::XSetTransientForHint(Libs::gtk_widget_get_window(gtkWidget), parent->winId());
	}

	if (modality != Qt::NonModal) {
		Libs::gdk_window_set_modal_hint(Libs::gtk_widget_get_window(gtkWidget), true);
		QGuiApplicationPrivate::showModalWindow(this);
	}

	Libs::gtk_widget_show(gtkWidget);
	Libs::gdk_window_focus(Libs::gtk_widget_get_window(gtkWidget), 0);
}

void QGtkDialog::hide() {
	QGuiApplicationPrivate::hideModalWindow(this);
	Libs::gtk_widget_hide(gtkWidget);
}

void QGtkDialog::onResponse(QGtkDialog *dialog, int response) {
	if (response == GTK_RESPONSE_OK)
		emit dialog->accept();
	else
		emit dialog->reject();
}

void QGtkDialog::onParentWindowDestroyed() {
	// The Gtk*DialogHelper classes own this object. Make sure the parent doesn't delete it.
	setParent(nullptr);
}

namespace {

const char *filterRegExp =
"^(.*)\\(([a-zA-Z0-9_.,*? +;#\\-\\[\\]@\\{\\}/!<>\\$%&=^~:\\|]*)\\)$";

// Makes a list of filters from a normal filter string "Image Files (*.png *.jpg)"
QStringList cleanFilterList(const QString &filter) {
	QRegExp regexp(QString::fromLatin1(filterRegExp));
	Q_ASSERT(regexp.isValid());
	QString f = filter;
	int i = regexp.indexIn(f);
	if (i >= 0)
		f = regexp.cap(2);
	return f.split(QLatin1Char(' '), QString::SkipEmptyParts);
}

} // namespace

GtkFileDialog::GtkFileDialog(QWidget *parent, const QString &caption, const QString &directory, const QString &filter) : QDialog(parent)
, _windowTitle(caption)
, _initialDirectory(directory) {
	auto filters = qt_make_filter_list(filter);
	const int numFilters = filters.count();
	_nameFilters.reserve(numFilters);
	for (int i = 0; i < numFilters; ++i) {
		_nameFilters << filters[i].simplified();
	}

	d.reset(new QGtkDialog(Libs::gtk_file_chooser_dialog_new("", nullptr,
		GTK_FILE_CHOOSER_ACTION_OPEN,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_OK, GTK_RESPONSE_OK, NULL)));
	connect(d.data(), SIGNAL(accept()), this, SLOT(onAccepted()));
	connect(d.data(), SIGNAL(reject()), this, SLOT(onRejected()));

	Libs::g_signal_connect_helper(Libs::gtk_file_chooser_cast(d->gtkDialog()), "selection-changed", G_CALLBACK(onSelectionChanged), this);
	Libs::g_signal_connect_swapped_helper(Libs::gtk_file_chooser_cast(d->gtkDialog()), "current-folder-changed", G_CALLBACK(onCurrentFolderChanged), this);
}

GtkFileDialog::~GtkFileDialog() {
}

void GtkFileDialog::showHelper(Qt::WindowFlags flags, Qt::WindowModality modality, QWindow *parent) {
	_dir.clear();
	_selection.clear();

	applyOptions();
	return d->show(flags, modality, parent);
}

void GtkFileDialog::setVisible(bool visible) {
	if (visible) {
		if (testAttribute(Qt::WA_WState_ExplicitShowHide) && !testAttribute(Qt::WA_WState_Hidden)) {
			return;
		}
	} else if (testAttribute(Qt::WA_WState_ExplicitShowHide) && testAttribute(Qt::WA_WState_Hidden)) {
		return;
	}

	if (visible) {
		showHelper(windowFlags(), windowModality(), parentWidget() ? parentWidget()->windowHandle() : nullptr);
	} else {
		hideHelper();
	}

	// Set WA_DontShowOnScreen so that QDialog::setVisible(visible) below
	// updates the state correctly, but skips showing the non-native version:
	setAttribute(Qt::WA_DontShowOnScreen);

	QDialog::setVisible(visible);
}

int GtkFileDialog::exec() {
	d->setModality(windowModality());

	bool deleteOnClose = testAttribute(Qt::WA_DeleteOnClose);
	setAttribute(Qt::WA_DeleteOnClose, false);

	bool wasShowModal = testAttribute(Qt::WA_ShowModal);
	setAttribute(Qt::WA_ShowModal, true);
	setResult(0);

	show();

	QPointer<QDialog> guard = this;
	d->exec();
	if (guard.isNull())
		return QDialog::Rejected;

	setAttribute(Qt::WA_ShowModal, wasShowModal);

	return result();
}

void GtkFileDialog::hideHelper() {
	// After GtkFileChooserDialog has been hidden, gtk_file_chooser_get_current_folder()
	// & gtk_file_chooser_get_filenames() will return bogus values -> cache the actual
	// values before hiding the dialog
	_dir = directory().absolutePath();
	_selection = selectedFiles();

	d->hide();
}

bool GtkFileDialog::defaultNameFilterDisables() const {
	return false;
}

void GtkFileDialog::setDirectory(const QString &directory) {
	GtkDialog *gtkDialog = d->gtkDialog();
	Libs::gtk_file_chooser_set_current_folder(Libs::gtk_file_chooser_cast(gtkDialog), directory.toUtf8());
}

QDir GtkFileDialog::directory() const {
	// While GtkFileChooserDialog is hidden, gtk_file_chooser_get_current_folder()
	// returns a bogus value -> return the cached value before hiding
	if (!_dir.isEmpty())
		return _dir;

	QString ret;
	GtkDialog *gtkDialog = d->gtkDialog();
	gchar *folder = Libs::gtk_file_chooser_get_current_folder(Libs::gtk_file_chooser_cast(gtkDialog));
	if (folder) {
		ret = QString::fromUtf8(folder);
		Libs::g_free(folder);
	}
	return QDir(ret);
}

void GtkFileDialog::selectFile(const QString &filename) {
	_initialFiles.clear();
	_initialFiles.append(filename);
}

QStringList GtkFileDialog::selectedFiles() const {
	// While GtkFileChooserDialog is hidden, gtk_file_chooser_get_filenames()
	// returns a bogus value -> return the cached value before hiding
	if (!_selection.isEmpty())
		return _selection;

	QStringList selection;
	GtkDialog *gtkDialog = d->gtkDialog();
	GSList *filenames = Libs::gtk_file_chooser_get_filenames(Libs::gtk_file_chooser_cast(gtkDialog));
	for (GSList *it  = filenames; it; it = it->next)
		selection += QString::fromUtf8((const char*)it->data);
	Libs::g_slist_free(filenames);
	return selection;
}

void GtkFileDialog::setFilter() {
	applyOptions();
}

void GtkFileDialog::selectNameFilter(const QString &filter) {
	GtkFileFilter *gtkFilter = _filters.value(filter);
	if (gtkFilter) {
		GtkDialog *gtkDialog = d->gtkDialog();
		Libs::gtk_file_chooser_set_filter(Libs::gtk_file_chooser_cast(gtkDialog), gtkFilter);
	}
}

QString GtkFileDialog::selectedNameFilter() const {
	GtkDialog *gtkDialog = d->gtkDialog();
	GtkFileFilter *gtkFilter = Libs::gtk_file_chooser_get_filter(Libs::gtk_file_chooser_cast(gtkDialog));
	return _filterNames.value(gtkFilter);
}

void GtkFileDialog::onAccepted() {
	emit accept();

//	QString filter = selectedNameFilter();
//	if (filter.isEmpty())
//		emit filterSelected(filter);

//	QList<QUrl> files = selectedFiles();
//	emit filesSelected(files);
//	if (files.count() == 1)
//		emit fileSelected(files.first());
}

void GtkFileDialog::onRejected() {
	emit reject();

//
}

void GtkFileDialog::onSelectionChanged(GtkDialog *gtkDialog, GtkFileDialog *helper) {
//	QString selection;
//	gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(gtkDialog));
//	if (filename) {
//		selection = QString::fromUtf8(filename);
//		g_free(filename);
//	}
//	emit helper->currentChanged(QUrl::fromLocalFile(selection));
}

void GtkFileDialog::onCurrentFolderChanged(GtkFileDialog *dialog) {
//	emit dialog->directoryEntered(dialog->directory());
}

GtkFileChooserAction gtkFileChooserAction(QFileDialog::FileMode fileMode, QFileDialog::AcceptMode acceptMode) {
	switch (fileMode) {
	case QFileDialog::AnyFile:
	case QFileDialog::ExistingFile:
	case QFileDialog::ExistingFiles:
		if (acceptMode == QFileDialog::AcceptOpen)
			return GTK_FILE_CHOOSER_ACTION_OPEN;
		else
			return GTK_FILE_CHOOSER_ACTION_SAVE;
	case QFileDialog::Directory:
	case QFileDialog::DirectoryOnly:
	default:
		if (acceptMode == QFileDialog::AcceptOpen)
			return GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
		else
			return GTK_FILE_CHOOSER_ACTION_CREATE_FOLDER;
	}
}

bool CustomButtonsSupported() {
	return (Libs::gtk_dialog_get_widget_for_response != nullptr)
		&& (Libs::gtk_button_set_label != nullptr)
		&& (Libs::gtk_button_get_type != nullptr);
}

void GtkFileDialog::applyOptions() {
	GtkDialog *gtkDialog = d->gtkDialog();

	Libs::gtk_window_set_title(Libs::gtk_window_cast(gtkDialog), _windowTitle.toUtf8());
	Libs::gtk_file_chooser_set_local_only(Libs::gtk_file_chooser_cast(gtkDialog), true);

	const GtkFileChooserAction action = gtkFileChooserAction(_fileMode, _acceptMode);
	Libs::gtk_file_chooser_set_action(Libs::gtk_file_chooser_cast(gtkDialog), action);

	const bool selectMultiple = (_fileMode == QFileDialog::ExistingFiles);
	Libs::gtk_file_chooser_set_select_multiple(Libs::gtk_file_chooser_cast(gtkDialog), selectMultiple);

	const bool confirmOverwrite = !_options.testFlag(QFileDialog::DontConfirmOverwrite);
	Libs::gtk_file_chooser_set_do_overwrite_confirmation(Libs::gtk_file_chooser_cast(gtkDialog), confirmOverwrite);

	if (!_nameFilters.isEmpty())
		setNameFilters(_nameFilters);

	if (!_initialDirectory.isEmpty())
		setDirectory(_initialDirectory);

	for_const (const auto &filename, _initialFiles) {
		if (_acceptMode == QFileDialog::AcceptSave) {
			QFileInfo fi(filename);
			Libs::gtk_file_chooser_set_current_folder(Libs::gtk_file_chooser_cast(gtkDialog), fi.path().toUtf8());
			Libs::gtk_file_chooser_set_current_name(Libs::gtk_file_chooser_cast(gtkDialog), fi.fileName().toUtf8());
		} else if (filename.endsWith('/')) {
			Libs::gtk_file_chooser_set_current_folder(Libs::gtk_file_chooser_cast(gtkDialog), filename.toUtf8());
		} else {
			Libs::gtk_file_chooser_select_filename(Libs::gtk_file_chooser_cast(gtkDialog), filename.toUtf8());
		}
	}

	const QString initialNameFilter = _nameFilters.isEmpty() ? QString() : _nameFilters.front();
	if (!initialNameFilter.isEmpty())
		selectNameFilter(initialNameFilter);

	if (CustomButtonsSupported()) {
		GtkWidget *acceptButton = Libs::gtk_dialog_get_widget_for_response(gtkDialog, GTK_RESPONSE_OK);
		if (acceptButton) {
			/*if (opts->isLabelExplicitlySet(QFileDialogOptions::Accept))
				Libs::gtk_button_set_label(Libs::gtk_button_cast(acceptButton), opts->labelText(QFileDialogOptions::Accept).toUtf8());
			else*/ if (_acceptMode == QFileDialog::AcceptOpen)
				Libs::gtk_button_set_label(Libs::gtk_button_cast(acceptButton), GTK_STOCK_OPEN);
			else
				Libs::gtk_button_set_label(Libs::gtk_button_cast(acceptButton), GTK_STOCK_SAVE);
		}

		GtkWidget *rejectButton = Libs::gtk_dialog_get_widget_for_response(gtkDialog, GTK_RESPONSE_CANCEL);
		if (rejectButton) {
			/*if (opts->isLabelExplicitlySet(QFileDialogOptions::Reject))
				Libs::gtk_button_set_label(Libs::gtk_button_cast(rejectButton), opts->labelText(QFileDialogOptions::Reject).toUtf8());
			else*/
				Libs::gtk_button_set_label(Libs::gtk_button_cast(rejectButton), GTK_STOCK_CANCEL);
		}
	}
}

void GtkFileDialog::setNameFilters(const QStringList &filters) {
	GtkDialog *gtkDialog = d->gtkDialog();
	foreach (GtkFileFilter *filter, _filters)
		Libs::gtk_file_chooser_remove_filter(Libs::gtk_file_chooser_cast(gtkDialog), filter);

	_filters.clear();
	_filterNames.clear();

	for_const (auto &filter, filters) {
		GtkFileFilter *gtkFilter = Libs::gtk_file_filter_new();
		auto name = filter;//.left(filter.indexOf(QLatin1Char('(')));
		auto extensions = cleanFilterList(filter);

		Libs::gtk_file_filter_set_name(gtkFilter, name.isEmpty() ? extensions.join(QStringLiteral(", ")).toUtf8() : name.toUtf8());
		for_const (auto &ext, extensions) {
			auto caseInsensitiveExt = QString();
			caseInsensitiveExt.reserve(4 * ext.size());
			for_const (auto ch, ext) {
				auto chLower = ch.toLower();
				auto chUpper = ch.toUpper();
				if (chLower != chUpper) {
					caseInsensitiveExt.append('[').append(chLower).append(chUpper).append(']');
				} else {
					caseInsensitiveExt.append(ch);
				}
			}

			Libs::gtk_file_filter_add_pattern(gtkFilter, caseInsensitiveExt.toUtf8());
		}

		Libs::gtk_file_chooser_add_filter(Libs::gtk_file_chooser_cast(gtkDialog), gtkFilter);

		_filters.insert(filter, gtkFilter);
		_filterNames.insert(gtkFilter, filter);
	}
}

} // namespace internal
} // namespace FileDialog
} // namespace Platform
