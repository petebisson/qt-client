/*
 * This file is part of the xTuple ERP: PostBooks Edition, a free and
 * open source Enterprise Resource Planning software suite,
 * Copyright (c) 1999-2010 by OpenMFG LLC, d/b/a xTuple.
 * It is licensed to you under the Common Public Attribution License
 * version 1.0, the full text of which (including xTuple-specific Exhibits)
 * is available at www.xtuple.com/CPAL.  By using this software, you agree
 * to be bound by its terms.
 */

#include "dspInventoryAvailabilityBySalesOrder.h"

#include <metasql.h>
#include <openreports.h>
#include <parameter.h>

#include <QMessageBox>
#include <QSqlError>
#include <QVariant>

#include "createCountTagsByItem.h"
#include "dspAllocations.h"
#include "dspOrders.h"
#include "dspReservations.h"
#include "dspRunningAvailability.h"
#include "dspSubstituteAvailabilityByItem.h"
#include "inputManager.h"
#include "mqlutil.h"
#include "purchaseOrder.h"
#include "reserveSalesOrderItem.h"
#include "salesOrderList.h"
#include "storedProcErrorLookup.h"
#include "workOrder.h"

dspInventoryAvailabilityBySalesOrder::dspInventoryAvailabilityBySalesOrder(QWidget* parent, const char* name, Qt::WFlags fl)
    : XWidget(parent, name, fl)
{
  setupUi(this);

  connect(_print, SIGNAL(clicked()), this, SLOT(sPrint()));
  connect(_onlyShowShortages, SIGNAL(clicked()), this, SLOT(sFillList()));
  connect(_showWoSupply, SIGNAL(clicked()), this, SLOT(sFillList()));
  connect(_so, SIGNAL(newId(int)), this, SLOT(sFillList()));
  connect(_salesOrderList, SIGNAL(clicked()), this, SLOT(sSoList()));
  connect(_avail, SIGNAL(populateMenu(QMenu*, QTreeWidgetItem*,int)), this, SLOT(sPopulateMenu(QMenu*, QTreeWidgetItem*)));
  connect(_so, SIGNAL(requestList()), this, SLOT(sSoList()));
  connect(_autoupdate, SIGNAL(toggled(bool)), this, SLOT(sAutoUpdateToggled(bool)));

#ifndef Q_WS_MAC
  _salesOrderList->setMaximumWidth(25);
#endif

  omfgThis->inputManager()->notify(cBCSalesOrder, this, _so, SLOT(setId(int)));

  _avail->addColumn(tr("Item Number"),_itemColumn, Qt::AlignLeft,  true, "item_number");
  _avail->addColumn(tr("Description"),         -1, Qt::AlignLeft,  true, "descrip");
  _avail->addColumn(tr("UOM"),         _uomColumn, Qt::AlignCenter,true, "uom_name");
  _avail->addColumn(tr("QOH"),         _qtyColumn, Qt::AlignRight, true, "qoh");
  _avail->addColumn(tr("This Alloc."), _qtyColumn, Qt::AlignRight, true, "sobalance");
  _avail->addColumn(tr("Total Alloc."),_qtyColumn, Qt::AlignRight, true, "allocated");
  _avail->addColumn(tr("Orders"),      _qtyColumn, Qt::AlignRight, true, "ordered");
  _avail->addColumn(tr("This Avail."), _qtyColumn, Qt::AlignRight, true, "orderavail");
  _avail->addColumn(tr("Total Avail."),_qtyColumn, Qt::AlignRight, true, "totalavail");
  _avail->addColumn(tr("At Shipping"), _qtyColumn, Qt::AlignRight, true, "atshipping");
  _avail->addColumn(tr("Start Date"), _dateColumn, Qt::AlignCenter,true, "orderdate");
  _avail->addColumn(tr("Sched. Date"),_dateColumn, Qt::AlignCenter,true, "duedate");
  _avail->setIndentation(10);

  if(!_metrics->boolean("EnableSOReservations"))
  {
    _useReservationNetting->hide();
    _useReservationNetting->setEnabled(false);
  }
  else
  {
    connect(_useReservationNetting, SIGNAL(toggled(bool)), this, SLOT(sHandleReservationNetting(bool)));
    if(_useReservationNetting->isChecked())
      sHandleReservationNetting(true);
  }
  connect(omfgThis, SIGNAL(workOrdersUpdated(int, bool)), this, SLOT(sFillList()));
  sAutoUpdateToggled(_autoupdate->isChecked());
}

dspInventoryAvailabilityBySalesOrder::~dspInventoryAvailabilityBySalesOrder()
{
  // no need to delete child widgets, Qt does it all for us
}

void dspInventoryAvailabilityBySalesOrder::languageChange()
{
  retranslateUi(this);
}

enum SetResponse dspInventoryAvailabilityBySalesOrder::set(const ParameterList &pParams)
{
  XWidget::set(pParams);
  QVariant param;
  bool     valid;

  param = pParams.value("onlyShowShortages", &valid);
  if (valid)
    _onlyShowShortages->setChecked(TRUE);

  param = pParams.value("sohead_id", &valid);
  if (valid)
    _so->setId(param.toInt());

  return NoError;
}

void dspInventoryAvailabilityBySalesOrder::sSoList()
{
  ParameterList params;
  params.append("sohead_id", _so->id());
  params.append("soType", cSoOpen);
  
  salesOrderList newdlg(this, "", TRUE);
  newdlg.set(params);

  int id = newdlg.exec();
  if(id != QDialog::Rejected)
    _so->setId(id);
}

bool dspInventoryAvailabilityBySalesOrder::setParams(ParameterList &params)
{
  if(!_so->isValid())
  {
    QMessageBox::warning(this, tr("No Sales Order Selected"),
      tr("You must select a valid Sales Order.") );
    return false;
  }

  params.append("sohead_id", _so->id());

  if(_onlyShowShortages->isChecked())
    params.append("onlyShowShortages");
  if (_showWoSupply->isChecked())
    params.append("showWoSupply");
  if (_useReservationNetting->isChecked())
    params.append("useReservationNetting");

  return true;
}

void dspInventoryAvailabilityBySalesOrder::sPrint()
{
  ParameterList params;
  if (! setParams(params))
    return;

  orReport report("InventoryAvailabilityBySalesOrder", params);
  if (report.isValid())
    report.print();
  else
    report.reportError(this);
}

void dspInventoryAvailabilityBySalesOrder::sPopulateMenu(QMenu *pMenu,  QTreeWidgetItem *selected)
{
  XTreeWidgetItem * item = (XTreeWidgetItem*)selected;
  int menuItem;
  
  if (_avail->altId() != -1)
  {
    menuItem = pMenu->insertItem("View Allocations...", this, SLOT(sViewAllocations()), 0);
    if (item->rawValue("allocated").toDouble() == 0.0)
      pMenu->setItemEnabled(menuItem, FALSE);
    
    menuItem = pMenu->insertItem("View Orders...", this, SLOT(sViewOrders()), 0);
    if (item->rawValue("ordered").toDouble() == 0.0)
     pMenu->setItemEnabled(menuItem, FALSE);

    menuItem = pMenu->insertItem("Running Availability...", this, SLOT(sRunningAvailability()), 0);
    menuItem = pMenu->insertItem("Substitute Availability...", this, SLOT(sViewSubstituteAvailability()), 0);

    q.prepare ("SELECT item_type "
             "FROM itemsite,item "
             "WHERE ((itemsite_id=:itemsite_id)"
             "AND (itemsite_item_id=item_id)"
             "AND (itemsite_wosupply));");
    q.bindValue(":itemsite_id", _avail->id());
    q.exec();
    if (q.next())
    {
      if (q.value("item_type") == "P")
      {
        pMenu->insertSeparator();
        menuItem = pMenu->insertItem("Issue Purchase Order...", this, SLOT(sIssuePO()), 0);
        if (!_privileges->check("MaintainPurchaseOrders"))
          pMenu->setItemEnabled(menuItem, FALSE);
      }
      else if (q.value("item_type") == "M")
      {
        pMenu->insertSeparator();
        menuItem = pMenu->insertItem("Issue Work Order...", this, SLOT(sIssueWO()), 0);
        if (!_privileges->check("MaintainWorkOrders"))
          pMenu->setItemEnabled(menuItem, FALSE);
      }
    }

    if(_metrics->boolean("EnableSOReservations"))
    {
      pMenu->insertSeparator();

      pMenu->insertItem(tr("Show Reservations..."), this, SLOT(sShowReservations()));
      pMenu->insertSeparator();

      int menuid;
      menuid = pMenu->insertItem(tr("Unreserve Stock"), this, SLOT(sUnreserveStock()), 0);
      pMenu->setItemEnabled(menuid, _privileges->check("MaintainReservations"));
      menuid = pMenu->insertItem(tr("Reserve Stock..."), this, SLOT(sReserveStock()), 0);
      pMenu->setItemEnabled(menuid, _privileges->check("MaintainReservations"));
      menuid = pMenu->insertItem(tr("Reserve Line Balance"), this, SLOT(sReserveLineBalance()), 0);
      pMenu->setItemEnabled(menuid, _privileges->check("MaintainReservations"));
    }

    pMenu->insertSeparator();
    menuItem = pMenu->insertItem("Issue Count Tag...", this, SLOT(sIssueCountTag()), 0);
    if (!_privileges->check("IssueCountTags"))
      pMenu->setItemEnabled(menuItem, FALSE);
  }
}

void dspInventoryAvailabilityBySalesOrder::sViewAllocations()
{
  q.prepare( "SELECT coitem_scheddate "
             "FROM coitem "
             "WHERE (coitem_id=:soitem_id);" );
  q.bindValue(":soitem_id", _avail->altId());
  q.exec();
  if (q.first())
  {
    ParameterList params;
    params.append("itemsite_id", _avail->id());
    params.append("byDate", q.value("coitem_scheddate"));
    params.append("run");

    dspAllocations *newdlg = new dspAllocations();
    newdlg->set(params);
    omfgThis->handleNewWindow(newdlg);
  }
}

void dspInventoryAvailabilityBySalesOrder::sViewOrders()
{
  q.prepare( "SELECT coitem_scheddate "
             "FROM coitem "
             "WHERE (coitem_id=:soitem_id);" );
  q.bindValue(":soitem_id", _avail->altId());
  q.exec();
  if (q.first())
  {
    ParameterList params;
    params.append("itemsite_id", _avail->id());
    params.append("byDate", q.value("coitem_scheddate"));
    params.append("run");

    dspOrders *newdlg = new dspOrders();
    newdlg->set(params);
    omfgThis->handleNewWindow(newdlg);
  }
}

void dspInventoryAvailabilityBySalesOrder::sRunningAvailability()
{
  ParameterList params;
  params.append("itemsite_id", _avail->id());
  params.append("run");

  dspRunningAvailability *newdlg = new dspRunningAvailability();
  newdlg->set(params);
  omfgThis->handleNewWindow(newdlg);
}

void dspInventoryAvailabilityBySalesOrder::sViewSubstituteAvailability()
{
  q.prepare( "SELECT coitem_scheddate "
             "FROM coitem "
             "WHERE (coitem_id=:soitem_id);" );
  q.bindValue(":soitem_id", _avail->altId());
  q.exec();
  if (q.first())
  {
    ParameterList params;
    params.append("itemsite_id", _avail->id());
    params.append("byDate", q.value("coitem_scheddate"));
    params.append("run");

    dspSubstituteAvailabilityByItem *newdlg = new dspSubstituteAvailabilityByItem();
    newdlg->set(params);
    omfgThis->handleNewWindow(newdlg);
  }
//  ToDo
}

void dspInventoryAvailabilityBySalesOrder::sIssuePO()
{
  ParameterList params;
  params.append("mode", "new");
  params.append("itemsite_id", _avail->id());

  purchaseOrder *newdlg = new purchaseOrder();
  if(newdlg->set(params) == NoError)
    omfgThis->handleNewWindow(newdlg);
}

void dspInventoryAvailabilityBySalesOrder::sIssueWO()
{
  ParameterList params;
  params.append("mode", "new");
  params.append("itemsite_id", _avail->id());

  workOrder *newdlg = new workOrder();
  newdlg->set(params);
  omfgThis->handleNewWindow(newdlg);
}

void dspInventoryAvailabilityBySalesOrder::sIssueCountTag()
{
  ParameterList params;
  params.append("itemsite_id", _avail->id());

  createCountTagsByItem newdlg(this, "", TRUE);
  newdlg.set(params);
  newdlg.exec();
}

void dspInventoryAvailabilityBySalesOrder::sFillList()
{
  q.prepare( "SELECT cohead_number,"
             "       cohead_orderdate,"
             "       cohead_custponumber,"
             "       cust_name, cust_phone "
             "FROM cohead, cust "
             "WHERE ( (cohead_cust_id=cust_id)"
             " AND (cohead_id=:sohead_id) );" );
  q.bindValue(":sohead_id", _so->id());
  q.exec();
  if (q.first())
  {
    _orderDate->setDate(q.value("cohead_orderdate").toDate());
    _poNumber->setText(q.value("cohead_custponumber").toString());
    _custName->setText(q.value("cust_name").toString());
    _custPhone->setText(q.value("cust_phone").toString());
  }
               
  ParameterList params;             
  if (! setParams(params))
    return;
  MetaSQLQuery mql = mqlLoad("inventoryAvailability", "byCustOrSO");
  q = mql.toQuery(params);
  _avail->populate(q, true);
  if (q.lastError().type() != QSqlError::NoError)
  {
    systemError(this, q.lastError().databaseText(), __FILE__, __LINE__);
    return;
  }
  _avail->expandAll();
}

void dspInventoryAvailabilityBySalesOrder::sAutoUpdateToggled(bool pAutoUpdate)
{
  if (pAutoUpdate)
    connect(omfgThis, SIGNAL(tick()), this, SLOT(sFillList()));
  else
    disconnect(omfgThis, SIGNAL(tick()), this, SLOT(sFillList()));
}

void dspInventoryAvailabilityBySalesOrder::sHandleReservationNetting(bool yn)
{
  if(yn)
    _avail->headerItem()->setText(7, tr("This Reserve"));
  else
    _avail->headerItem()->setText(7, tr("This Avail."));
  sFillList();
}

void dspInventoryAvailabilityBySalesOrder::sReserveStock()
{
  ParameterList params;
  params.append("soitem_id", _avail->altId());

  reserveSalesOrderItem newdlg(this, "", true);
  newdlg.set(params);
  if(newdlg.exec() == XDialog::Accepted)
    sFillList();
}

void dspInventoryAvailabilityBySalesOrder::sReserveLineBalance()
{
  q.prepare("SELECT reserveSoLineBalance(:soitem_id) AS result;");
  q.bindValue(":soitem_id", _avail->altId());
  q.exec();
  if (q.first())
  {
    int result = q.value("result").toInt();
    if (result < 0)
    {
      systemError(this, storedProcErrorLookup("reserveSoLineBalance", result),
                  __FILE__, __LINE__);
      return;
    }
  }
  else if (q.lastError().type() != QSqlError::NoError)
  {
    systemError(this, tr("Error\n") +
                      q.lastError().databaseText(), __FILE__, __LINE__);
    return;
  }

  sFillList();
}

void dspInventoryAvailabilityBySalesOrder::sUnreserveStock()
{
  q.prepare("UPDATE coitem SET coitem_qtyreserved=0 WHERE coitem_id=:soitem_id;");
  q.bindValue(":soitem_id", _avail->altId());
  q.exec();
  if (q.lastError().type() != QSqlError::NoError)
  {
    systemError(this, tr("Error\n") +
                      q.lastError().databaseText(), __FILE__, __LINE__);
    return;
  }

  sFillList();
}

void dspInventoryAvailabilityBySalesOrder::sShowReservations()
{
  ParameterList params;
  params.append("soitem_id", _avail->altId());
  params.append("run");

  dspReservations * newdlg = new dspReservations();
  newdlg->set(params);
  omfgThis->handleNewWindow(newdlg);
}
