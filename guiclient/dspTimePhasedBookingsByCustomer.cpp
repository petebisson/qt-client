/*
 * This file is part of the xTuple ERP: PostBooks Edition, a free and
 * open source Enterprise Resource Planning software suite,
 * Copyright (c) 1999-2010 by OpenMFG LLC, d/b/a xTuple.
 * It is licensed to you under the Common Public Attribution License
 * version 1.0, the full text of which (including xTuple-specific Exhibits)
 * is available at www.xtuple.com/CPAL.  By using this software, you agree
 * to be bound by its terms.
 */

#include "dspTimePhasedBookingsByCustomer.h"

#include <QVariant>
#include <QWorkspace>
#include <QMessageBox>
//#include <QStatusBar>
#include <QMenu>
#include <q3valuevector.h>
#include <datecluster.h>
#include <parameter.h>
#include <openreports.h>
#include "guiclient.h"
#include "dspBookingsByCustomer.h"
#include "submitReport.h"

/*
 *  Constructs a dspTimePhasedBookingsByCustomer as a child of 'parent', with the
 *  name 'name' and widget flags set to 'f'.
 *
 */
dspTimePhasedBookingsByCustomer::dspTimePhasedBookingsByCustomer(QWidget* parent, const char* name, Qt::WFlags fl)
    : XWidget(parent, name, fl)
{
  setupUi(this);

//  (void)statusBar();

  // signals and slots connections
  connect(_print, SIGNAL(clicked()), this, SLOT(sPrint()));
  connect(_soitem, SIGNAL(populateMenu(QMenu*,QTreeWidgetItem*,int)), this, SLOT(sPopulateMenu(QMenu*,QTreeWidgetItem*,int)));
  connect(_close, SIGNAL(clicked()), this, SLOT(close()));
  connect(_calendar, SIGNAL(newCalendarId(int)), _periods, SLOT(populate(int)));
  connect(_query, SIGNAL(clicked()), this, SLOT(sFillList()));
  connect(_calendar, SIGNAL(select(ParameterList&)), _periods, SLOT(load(ParameterList&)));
  connect(_submit, SIGNAL(clicked()), this, SLOT(sSubmit()));
  
  if (!_metrics->boolean("EnableBatchManager"))
    _submit->hide();
    
  _customerType->setType(ParameterGroup::CustomerType);

  _soitem->addColumn(tr("Cust. #"),  _orderColumn, Qt::AlignLeft,  true,  "cust_number" );
  _soitem->addColumn(tr("Customer"), 180,          Qt::AlignLeft,  true,  "cust_name" );
}

/*
 *  Destroys the object and frees any allocated resources
 */
dspTimePhasedBookingsByCustomer::~dspTimePhasedBookingsByCustomer()
{
  // no need to delete child widgets, Qt does it all for us
}

/*
 *  Sets the strings of the subwidgets using the current
 *  language.
 */
void dspTimePhasedBookingsByCustomer::languageChange()
{
  retranslateUi(this);
}

void dspTimePhasedBookingsByCustomer::sPrint()
{
  if (_periods->isPeriodSelected())
  {
    orReport report("TimePhasedBookingsByCustomer", buildParameters());
    if (report.isValid())
      report.print();
    else
    {
      report.reportError(this);
      return;
    }
  }
  else
    QMessageBox::critical( this, tr("Incomplete criteria"),
                           tr( "The criteria you specified is not complete. Please make sure all\n"
                               "fields are correctly filled out before running the report." ) );
}

void dspTimePhasedBookingsByCustomer::sViewBookings()
{
  if (_column > 1)
  {
    ParameterList params;
    params.append("cust_id", _soitem->id());
    params.append("startDate", _columnDates[_column - 2].startDate);
    params.append("endDate", _columnDates[_column - 2].endDate);
    params.append("run");

    dspBookingsByCustomer *newdlg = new dspBookingsByCustomer();
    newdlg->set(params);
    omfgThis->handleNewWindow(newdlg);
  }
}

void dspTimePhasedBookingsByCustomer::sPopulateMenu(QMenu *pMenu, QTreeWidgetItem *pSelected, int pColumn)
{
  int menuItem;

  _column = pColumn;

  if (((XTreeWidgetItem *)pSelected)->id() != -1)
    menuItem = pMenu->insertItem(tr("View Bookings..."), this, SLOT(sViewBookings()), 0);
}

void dspTimePhasedBookingsByCustomer::sFillList()
{
  if (!_periods->isPeriodSelected())
  {
    if (isVisible())
      QMessageBox::warning( this, tr("Select Calendar Periods"),
                            tr("Please select one or more Calendar Periods") );

    return;
  }

  _soitem->clear();
  _soitem->setColumnCount(2);

  _columnDates.clear();

  QString sql("SELECT cust_id, cust_number, cust_name");

  int columns = 1;
  QList<XTreeWidgetItem*> selected = _periods->selectedItems();
  for (int i = 0; i < selected.size(); i++)
  {
    PeriodListViewItem *cursor = (PeriodListViewItem*)selected[i];
    QString bucketname = QString("bucket%1").arg(columns++);
    sql += QString(", bookingsByCustomerValue(cust_id, %1) AS %2,"
                   "  'curr' AS %3_xtnumericrole, 0 AS %4_xttotalrole ")
	     .arg(cursor->id())
	     .arg(bucketname)
	     .arg(bucketname)
	     .arg(bucketname);

    _soitem->addColumn(formatDate(cursor->startDate()), _qtyColumn, Qt::AlignRight, true, bucketname);
    _columnDates.append(DatePair(cursor->startDate(), cursor->endDate()));
  }

  sql += " FROM cust ";

  if (_customerType->isSelected())
    sql += "WHERE (cust_custtype_id=:custtype_id) ";
  else if (_customerType->isPattern())
    sql += "WHERE (cust_custtype_id IN (SELECT custtype_id FROM custtype WHERE (custtype_code ~ :custtype_pattern))) ";

  sql += "ORDER BY cust_number;";

  q.prepare(sql);
  _customerType->bindValue(q);
  q.exec();
  _soitem->populate(q);
}

void dspTimePhasedBookingsByCustomer::sSubmit()
{
  if (_periods->isPeriodSelected())
  {
    ParameterList params(buildParameters());
    params.append("report_name", "TimePhasedBookingsByCustomer");
    
    submitReport newdlg(this, "", TRUE);
    newdlg.set(params);

    if (newdlg.check() == cNoReportDefinition)
      QMessageBox::critical( this, tr("Report Definition Not Found"),
                             tr( "The report defintions for this report, \"TimePhasedBookingsByCustomer\" cannot be found.\n"
                                 "Please contact your Systems Administrator and report this issue." ) );
    else
      newdlg.exec();
  }
  else
    QMessageBox::critical( this, tr("Incomplete criteria"),
                           tr( "The criteria you specified is not complete. Please make sure all\n"
                               "fields are correctly filled out before running the report." ) );
}

ParameterList dspTimePhasedBookingsByCustomer::buildParameters()
{
  ParameterList params;

  _customerType->appendValue(params);

  QList<XTreeWidgetItem*> selected = _periods->selectedItems();
  QList<QVariant> periodList;
  for (int i = 0; i < selected.size(); i++)
    periodList.append(((XTreeWidgetItem*)selected[i])->id());

  params.append("period_id_list", periodList);

  return params;
}