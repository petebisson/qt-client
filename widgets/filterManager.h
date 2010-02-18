/*
 * This file is part of the xTuple ERP: PostBooks Edition, a free and
 * open source Enterprise Resource Planning software suite,
 * Copyright (c) 1999-2009 by OpenMFG LLC, d/b/a xTuple.
 * It is licensed to you under the Common Public Attribution License
 * version 1.0, the full text of which (including xTuple-specific Exhibits)
 * is available at www.xtuple.com/CPAL.  By using this software, you agree
 * to be bound by its terms.
 */

#ifndef FILTERMANAGER_H
#define FILTERMANAGER_H

#include "parameter.h"

#include "ui_filterManager.h"

class filterManager : public QDialog, public Ui::filterManager
{
    Q_OBJECT

public:
    filterManager(QWidget* parent = 0, const char* name = 0);
    void set( ParameterList & pParams );
    void populate();
	
public slots:
    void getXTreeWidgetItem(XTreeWidgetItem* item, int column);
    void applySaved();
    void deleteFilter();

signals:
    void filterSelected(QString);
    void filterDeleted();

private:
   int _filterId;
   QString _filterText;
   QString _screen;
};

#endif // FILTERMANAGER_H