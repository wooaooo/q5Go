/*
 *
 *  q G o   a Go Program using Trolltech's Qt
 *
 *  (C) by Peter Strempel, Johannes Mesa, Emmanuel Beranger 2001-2003
 *
 */

#include <QFileDialog>

#include "config.h"
#include "sgf.h"
#include "clientwin.h"
#include "msg_handler.h"
#include "setting.h"
#include "miscdialogs.h"
#include "ui_helpers.h"
#include "variantgamedlg.h"

#include <fstream>
#include <qtranslator.h>
#include <qtextcodec.h>
#include <qapplication.h>
#include <qdialog.h>
#include <qmessagebox.h>
#include <qdir.h>

qGo *qgo;
QApplication *qgo_app;

// global
Setting *setting = 0;

QString program_dir;

#ifdef OWN_DEBUG_MODE
QTextEdit *debug_view;
#endif

std::shared_ptr<game_record> new_game_dialog (QWidget *parent)
{
	NewLocalGameDialog dlg(parent);

	if (dlg.exec() != QDialog::Accepted)
		return nullptr;

	int sz = dlg.boardSizeSpin->value();
	int hc = dlg.handicapSpin->value();
	go_board starting_pos = new_handicap_board (sz, hc);
	game_info info ("",
			dlg.playerWhiteEdit->text().toStdString (),
			dlg.playerBlackEdit->text().toStdString (),
			dlg.playerWhiteRkEdit->text().toStdString (),
			dlg.playerBlackRkEdit->text().toStdString (),
			"", dlg.komiSpin->value(), hc,
			ranked::free,
			"", "", "", "", "", "", -1);
	std::shared_ptr<game_record> gr = std::make_shared<game_record> (starting_pos, hc > 1 ? white : black, info);

	return gr;
}

std::shared_ptr<game_record> new_variant_game_dialog (QWidget *parent)
{
	NewVariantGameDialog dlg(parent);

	if (dlg.exec() != QDialog::Accepted)
		return nullptr;

	int sz_x = dlg.xSizeSpin->value();
	int sz_y = dlg.ySizeSpin->value();
	bool torus_h = dlg.hTorusCheckBox->isChecked ();
	bool torus_v = dlg.vTorusCheckBox->isChecked ();
	go_board starting_pos (sz_x, sz_y, torus_h, torus_v);
	game_info info ("",
			dlg.playerWhiteEdit->text().toStdString (),
			dlg.playerBlackEdit->text().toStdString (),
			dlg.playerWhiteRkEdit->text().toStdString (),
			dlg.playerBlackRkEdit->text().toStdString (),
			"", dlg.komiSpin->value(), 0,
			ranked::free,
			"", "", "", "", "", "", -1);
	std::shared_ptr<game_record> gr = std::make_shared<game_record> (starting_pos, black, info);

	return gr;
}

/* A wrapper around sgf2record to handle exceptions with message boxes.  */

std::shared_ptr<game_record> record_from_stream (std::istream &isgf)
{
	try {
		sgf *sgf = load_sgf (isgf);
		std::shared_ptr<game_record> gr = sgf2record (*sgf);
		const sgf_errors &errs = gr->errors ();
		if (errs.played_on_stone) {
			QMessageBox::warning (0, PACKAGE, QObject::tr ("The SGF file contained an invalid move that was played on top of another stone. Variations have been truncated at that point."));
		}
		if (errs.charset_error) {
			QMessageBox::warning (0, PACKAGE, QObject::tr ("One or more comments have been dropped since they contained invalid characters."));
		}
		return gr;
	} catch (invalid_boardsize &) {
		QMessageBox::warning (0, PACKAGE, QObject::tr ("Unsupported board size in SGF file."));
	} catch (broken_sgf &) {
		QMessageBox::warning (0, PACKAGE, QObject::tr ("Errors found in SGF file."));
	} catch (...) {
		QMessageBox::warning (0, PACKAGE, QObject::tr ("Error while trying to load SGF file."));
	}
	return nullptr;
}

bool open_window_from_file (const std::string &filename)
{
	std::ifstream isgf (filename);
	std::shared_ptr<game_record> gr = record_from_stream (isgf);
	if (gr == nullptr)
		return false;

	gr->set_filename (filename);
	MainWindow *win = new MainWindow (0, gr);
	win->show ();
	return true;
}

void open_local_board (QWidget *parent, game_dialog_type type)
{
	std::shared_ptr<game_record> gr;
	switch (type) {
	case game_dialog_type::normal:
		gr = new_game_dialog (parent);
		if (gr == nullptr)
			return;
		break;
	case game_dialog_type::variant:
		gr = new_variant_game_dialog (parent);
		if (gr == nullptr)
			return;
		break;

	case game_dialog_type::none:
	{
		go_board b (19);
		gr = std::make_shared<game_record> (b, black, game_info (QObject::tr ("White").toStdString (),
									 QObject::tr ("Black").toStdString ()));
		break;
	}
	}
	MainWindow *win = new MainWindow (0, gr);
	win->show ();
}

/* Create a bit array of hoshi points for a board shaped like REF.  */
bit_array calculate_hoshis (const go_board &ref)
{
	int size_x = ref.size_x ();
	int size_y = ref.size_y ();
	bit_array map (ref.bitsize ());

	int edge_dist_x = size_x > 12 ? 4 : 3;
	int edge_dist_y = size_y > 12 ? 4 : 3;
	int low_x = edge_dist_x - 1;
	int low_y = edge_dist_y - 1;
	int middle_x = size_x / 2;
	int middle_y = size_y / 2;
	int high_x = size_x - edge_dist_x;
	int high_y = size_y - edge_dist_y;
	if (size_x % 2 && size_x > 9) {
		map.set_bit (ref.bitpos (middle_x, low_y));
		map.set_bit (ref.bitpos (middle_x, high_y));
		if (size_y % 2 && size_y > 9)
			map.set_bit (ref.bitpos (middle_x, middle_y));
	}
	if (size_y % 2 && size_y > 9) {
		map.set_bit (ref.bitpos (low_x, middle_y));
		map.set_bit (ref.bitpos (high_x, middle_y));
	}

	map.set_bit (ref.bitpos (low_x, low_y));
	map.set_bit (ref.bitpos (high_x, low_y));
	map.set_bit (ref.bitpos (high_x, high_y));
	map.set_bit (ref.bitpos (low_x, high_y));
	return map;
}

go_board new_handicap_board (int size, int handicap)
{
	go_board b (size);

	if (size > 25 || size < 7)
	{
		qWarning("*** BoardHandler::setHandicap() - can't set handicap for this board size");
		return b;
	}

	int edge_dist = size > 12 ? 4 : 3;
	int low = edge_dist - 1;
	int middle = size / 2;
	int high = size - edge_dist;

	// extra:
	if (size == 19 && handicap > 9)
		switch (handicap)
		{
		case 13:
			b.set_stone (16, 16, black);
		case 12:
			b.set_stone (2, 2, black);
		case 11:
			b.set_stone (2, 16, black);
		case 10:
			b.set_stone (16, 2, black);
		default:
			handicap = 9;
			break;
		}

	switch (size % 2)
	{
	// odd board size
	case 1:
		switch (handicap)
		{
		case 9:
			b.set_stone (middle, middle, black);
		case 8:
		case 7:
			if (handicap >= 8)
			{
				b.set_stone (middle, low, black);
				b.set_stone (middle, high, black);
			}
			else
				b.set_stone (middle, middle, black);
		case 6:
		case 5:
			if (handicap >= 6)
			{
				b.set_stone (low, middle, black);
				b.set_stone (high, middle, black);
			}
			else
				b.set_stone (middle, middle, black);
		case 4:
			b.set_stone (high, high, black);
		case 3:
			b.set_stone (low, low, black);
		case 2:
			b.set_stone (high, low, black);
			b.set_stone (low, high, black);
		case 1:
			break;
		default:
			qWarning("*** BoardHandler::setHandicap() - Invalid handicap given: %d", handicap);
		}
		break;

	// even board size
	case 0:
		switch (handicap)
		{
		case 4:
			b.set_stone (high, high, black);
		case 3:
			b.set_stone (low, low, black);
		case 2:
			b.set_stone (high, low, black);
			b.set_stone (low, high, black);
		case 1:
			break;

		default:
			qWarning("*** BoardHandler::setHandicap() - Invalid handicap given: %d", handicap);
		}
		break;

	default:
		qWarning("*** BoardHandler::setHandicap() - can't set handicap for this board size");

	}
	b.identify_units ();
	return b;
}

/* Generate a candidate for the filename for this game */
std::string get_candidate_filename (const std::string &dir, const game_info &info)
{
	const std::string &pw = info.name_white ();
	const std::string &pb = info.name_black ();
	QString date = QDate::currentDate().toString("yyyy-MM-dd");
	std::string cand = date.toStdString() + "-" + pw + "-" + pb;
	std::string base = cand;
	int i = 1;
	while (QFile(QString::fromStdString (dir) + QString::fromStdString (cand) + ".sgf").exists())
	{
		//number = Q.number(i++);
		cand = base + "-" + QString::number(i++).toStdString ();
		//fileName = fileName + ".sgf";
	}
	return dir + cand + ".sgf";
}

int main(int argc, char **argv)
{
	bool found_debug = false;
	bool found_sgf = false;
	const char *sgf_file;

	// look for arguments
	int ac = argc;
	while (ac--)
	{
		if (argv[ac] == QString("-debug"))
		{
			// view debug window
			found_debug = true;
		}
		else if (argv[ac] == QString("-sgf"))
		{
			// view board
			found_sgf = true;
		}
		else if (argv[ac] == QString("-sgf19"))
		{
			// set up board 19x19 immediately
			found_sgf = true;
			sgf_file = "/19/";
		}
		else if (argv[ac] == QString("-desktop"))
		{
			// set standard options
			qApp->setDesktopSettingsAware(true);
		}
		else if (ac && argv[ac][0] != '-')
		{
			// file name
			found_sgf = true;
			sgf_file = argv[ac];
		}
	}

	QApplication myapp(argc, argv);
	qgo_app = &myapp;

	myapp.setAttribute (Qt::AA_EnableHighDpiScaling);

#ifdef OWN_DEBUG_MODE
	qInstallMessageHandler(myMessageHandler);
	Debug_Dialog *nonModal = new Debug_Dialog();
	debug_view = nonModal->TextView1;
#endif

	// get application path
	QFileInfo program(argv[0]);
	program_dir = program.absolutePath ();
	qDebug() << "main:qt->PROGRAM.DIRPATH = " << program_dir;

	// restore last setting
	setting = new Setting();

	// load values from file
	setting->loadSettings();

	// Load translation
	QTranslator trans(0);
	QString lang = setting->getLanguage();
	qDebug() << "Checking for language settings..." << lang;
	QString tr_dir = setting->getTranslationsDirectory(), loc;

	if (lang.isEmpty ())
	{
		QLocale locale = QLocale::system ();
		qDebug() << "No language settings found, using system locale %s" << locale.name ();
		loc = QString("qgo_") + locale.language ();
	}
	else
	{
		qDebug () << "Language settings found: " + lang;
		loc = QString("qgo_") + lang;
	}

	if (trans.load(loc, tr_dir))
	{
		qDebug () << "Translation loaded.";
		myapp.installTranslator(&trans);
	} else if (lang != "en" && lang != "C")  // Skip warning for en and C default.
		qWarning() << "Failed to find translation file for " << lang;

	client_window = new ClientWindow(0);

#ifdef OWN_DEBUG_MODE
	// restore size and pos
	if (client_window->getViewSize().width() > 0)
	{
		nonModal->resize(client_window->getViewSize());
		nonModal->move(client_window->getViewPos());
	}

	if (found_debug)
		nonModal->show();
	else
		nonModal->hide();

	// for storing size
	client_window->setDebugDialog(nonModal);
#endif

	// if debugging allow enhanced output
	if (found_debug)
		client_window->DODEBUG = true;

	client_window->hide();
#if 0
	// set main widget
	myapp.setMainWidget(client_window);
#endif

	QObject::connect(&myapp, &QApplication::lastWindowClosed, client_window, &ClientWindow::slot_last_window_closed);

	if (found_sgf) {
		if (!open_window_from_file (sgf_file))
			return 1;
	} else {
		client_window->setWindowTitle (PACKAGE1 + QString(" V") + VERSION);
		client_window->show();
	}

	if (setting->getNewVersionWarning())
		help_new_version ();

	return myapp.exec();
}

