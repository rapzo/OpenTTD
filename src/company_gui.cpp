/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file company_gui.cpp %Company related GUIs. */

#include "stdafx.h"
#include "currency.h"
#include "error.h"
#include "gui.h"
#include "window_gui.h"
#include "textbuf_gui.h"
#include "viewport_func.h"
#include "company_func.h"
#include "command_func.h"
#include "network/network.h"
#include "network/network_gui.h"
#include "network/network_func.h"
#include "newgrf.h"
#include "company_manager_face.h"
#include "strings_func.h"
#include "date_func.h"
#include "widgets/dropdown_type.h"
#include "tilehighlight_func.h"
#include "company_base.h"
#include "core/geometry_func.hpp"
#include "object_type.h"
#include "rail.h"
#include "road.h"
#include "engine_base.h"
#include "window_func.h"
#include "road_func.h"
#include "water.h"
#include "station_func.h"
#include "zoom_func.h"
#include "sortlist_type.h"
#include "company_cmd.h"
#include "economy_cmd.h"
#include "group_cmd.h"
#include "misc_cmd.h"
#include "object_cmd.h"

#include "widgets/company_widget.h"

#include "safeguards.h"


/** Company GUI constants. */
static void DoSelectCompanyManagerFace(Window *parent);
static void ShowCompanyInfrastructure(CompanyID company);

/** List of revenues. */
static ExpensesType _expenses_list_revenue[] = {
	EXPENSES_TRAIN_REVENUE,
	EXPENSES_ROADVEH_REVENUE,
	EXPENSES_AIRCRAFT_REVENUE,
	EXPENSES_SHIP_REVENUE,
};

/** List of operating expenses. */
static ExpensesType _expenses_list_operating_costs[] = {
	EXPENSES_TRAIN_RUN,
	EXPENSES_ROADVEH_RUN,
	EXPENSES_AIRCRAFT_RUN,
	EXPENSES_SHIP_RUN,
	EXPENSES_PROPERTY,
	EXPENSES_LOAN_INTEREST,
};

/** List of capital expenses. */
static ExpensesType _expenses_list_capital_costs[] = {
	EXPENSES_CONSTRUCTION,
	EXPENSES_NEW_VEHICLES,
	EXPENSES_OTHER,
};

/** Expense list container. */
struct ExpensesList {
	const ExpensesType *et;   ///< Expenses items.
	const uint length;        ///< Number of items in list.

	ExpensesList(ExpensesType *et, int length) : et(et), length(length)
	{
	}

	uint GetHeight() const
	{
		/* Add up the height of all the lines.  */
		return this->length * FONT_HEIGHT_NORMAL;
	}

	/** Compute width of the expenses categories in pixels. */
	uint GetListWidth() const
	{
		uint width = 0;
		for (uint i = 0; i < this->length; i++) {
			ExpensesType et = this->et[i];
			width = std::max(width, GetStringBoundingBox(STR_FINANCES_SECTION_CONSTRUCTION + et).width);
		}
		return width;
	}
};

/** Types of expense lists */
static const ExpensesList _expenses_list_types[] = {
	ExpensesList(_expenses_list_revenue, lengthof(_expenses_list_revenue)),
	ExpensesList(_expenses_list_operating_costs, lengthof(_expenses_list_operating_costs)),
	ExpensesList(_expenses_list_capital_costs, lengthof(_expenses_list_capital_costs)),
};

/**
 * Get the total height of the "categories" column.
 * @return The total height in pixels.
 */
static uint GetTotalCategoriesHeight()
{
	/* There's an empty line and blockspace on the year row */
	uint total_height = FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_wide;

	for (uint i = 0; i < lengthof(_expenses_list_types); i++) {
		/* Title + expense list + total line + total + blockspace after category */
		total_height += FONT_HEIGHT_NORMAL + _expenses_list_types[i].GetHeight() + WidgetDimensions::scaled.vsep_normal + FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_wide;
	}

	/* Total income */
	total_height += WidgetDimensions::scaled.vsep_normal + FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_wide;

	return total_height;
}

/**
 * Get the required width of the "categories" column, equal to the widest element.
 * @return The required width in pixels.
 */
static uint GetMaxCategoriesWidth()
{
	uint max_width = 0;

	/* Loop through categories to check max widths. */
	for (uint i = 0; i < lengthof(_expenses_list_types); i++) {
		/* Title of category */
		max_width = std::max(max_width, GetStringBoundingBox(STR_FINANCES_REVENUE_TITLE + i).width);
		/* Entries in category */
		max_width = std::max(max_width, _expenses_list_types[i].GetListWidth() + WidgetDimensions::scaled.hsep_indent);
	}

	return max_width;
}

/**
 * Draw a category of expenses (revenue, operating expenses, capital expenses).
 */
static void DrawCategory(const Rect &r, int start_y, ExpensesList list)
{
	Rect tr = r.Indent(WidgetDimensions::scaled.hsep_indent, _current_text_dir == TD_RTL);

	tr.top = start_y;
	ExpensesType et;

	for (uint i = 0; i < list.length; i++) {
		et = list.et[i];
		DrawString(tr, STR_FINANCES_SECTION_CONSTRUCTION + et);
		tr.top += FONT_HEIGHT_NORMAL;
	}
}

/**
 * Draw the expenses categories.
 * @param r Available space for drawing.
 * @note The environment must provide padding at the left and right of \a r.
 */
static void DrawCategories(const Rect &r)
{
	/* Start with an empty space in the year row, plus the blockspace under the year. */
	int y = r.top + FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_wide;

	for (uint i = 0; i < lengthof(_expenses_list_types); i++) {
		/* Draw category title and advance y */
		DrawString(r.left, r.right, y, (STR_FINANCES_REVENUE_TITLE + i), TC_FROMSTRING, SA_LEFT);
		y += FONT_HEIGHT_NORMAL;

		/* Draw category items and advance y */
		DrawCategory(r, y, _expenses_list_types[i]);
		y += _expenses_list_types[i].GetHeight();

		/* Advance y by the height of the horizontal line between amounts and subtotal */
		y += WidgetDimensions::scaled.vsep_normal;

		/* Draw category total and advance y */
		DrawString(r.left, r.right, y, STR_FINANCES_TOTAL_CAPTION, TC_FROMSTRING, SA_RIGHT);
		y += FONT_HEIGHT_NORMAL;

		/* Advance y by a blockspace after this category block */
		y += WidgetDimensions::scaled.vsep_wide;
	}

	/* Draw total profit/loss */
	y += WidgetDimensions::scaled.vsep_normal;
	DrawString(r.left, r.right, y, STR_FINANCES_PROFIT, TC_FROMSTRING, SA_LEFT);
}

/**
 * Draw an amount of money.
 * @param amount Amount of money to draw,
 * @param left   Left coordinate of the space to draw in.
 * @param right  Right coordinate of the space to draw in.
 * @param top    Top coordinate of the space to draw in.
 * @param colour The TextColour of the string.
 */
static void DrawPrice(Money amount, int left, int right, int top, TextColour colour)
{
	StringID str = STR_FINANCES_NEGATIVE_INCOME;
	if (amount == 0) {
		str = STR_FINANCES_ZERO_INCOME;
	} else if (amount < 0) {
		amount = -amount;
		str = STR_FINANCES_POSITIVE_INCOME;
	}
	SetDParam(0, amount);
	DrawString(left, right, top, str, colour, SA_RIGHT);
}

/**
 * Draw a category of expenses/revenues in the year column.
 * @return The income sum of the category.
 */
static Money DrawYearCategory (const Rect &r, int start_y, ExpensesList list, const Money(&tbl)[EXPENSES_END])
{
	int y = start_y;
	ExpensesType et;
	Money sum = 0;

	for (uint i = 0; i < list.length; i++) {
		et = list.et[i];
		Money cost = tbl[et];
		sum += cost;
		if (cost != 0) DrawPrice(cost, r.left, r.right, y, TC_BLACK);
		y += FONT_HEIGHT_NORMAL;
	}

	/* Draw the total at the bottom of the category. */
	GfxFillRect(r.left, y, r.right, y, PC_BLACK);
	y += WidgetDimensions::scaled.vsep_normal;
	if (sum != 0) DrawPrice(sum, r.left, r.right, y, TC_WHITE);

	/* Return the sum for the yearly total. */
	return sum;
}


/**
 * Draw a column with prices.
 * @param r    Available space for drawing.
 * @param year Year being drawn.
 * @param tbl  Reference to table of amounts for \a year.
 * @note The environment must provide padding at the left and right of \a r.
 */
static void DrawYearColumn(const Rect &r, int year, const Money (&tbl)[EXPENSES_END])
{
	int y = r.top;
	Money sum;

	/* Year header */
	SetDParam(0, year);
	DrawString(r.left, r.right, y, STR_FINANCES_YEAR, TC_FROMSTRING, SA_RIGHT, true);
	y += FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_wide;

	/* Categories */
	for (uint i = 0; i < lengthof(_expenses_list_types); i++) {
		y += FONT_HEIGHT_NORMAL;
		sum += DrawYearCategory(r, y, _expenses_list_types[i], tbl);
		/* Expense list + expense category title + expense category total + blockspace after category */
		y += _expenses_list_types[i].GetHeight() + WidgetDimensions::scaled.vsep_normal + FONT_HEIGHT_NORMAL + WidgetDimensions::scaled.vsep_wide;
	}

	/* Total income. */
	GfxFillRect(r.left, y, r.right, y, PC_BLACK);
	y += WidgetDimensions::scaled.vsep_normal;
	DrawPrice(sum, r.left, r.right, y, TC_WHITE);
}

static const NWidgetPart _nested_company_finances_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_CF_CAPTION), SetDataTip(STR_FINANCES_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_CF_TOGGLE_SIZE), SetDataTip(SPR_LARGE_SMALL_WINDOW, STR_TOOLTIP_TOGGLE_LARGE_SMALL_WINDOW),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(NWID_SELECTION, INVALID_COLOUR, WID_CF_SEL_PANEL),
		NWidget(WWT_PANEL, COLOUR_GREY),
			NWidget(NWID_HORIZONTAL), SetPadding(WidgetDimensions::unscaled.framerect), SetPIP(0, WidgetDimensions::unscaled.hsep_wide, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CF_EXPS_CATEGORY), SetMinimalSize(120, 0), SetFill(0, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CF_EXPS_PRICE1), SetMinimalSize(86, 0), SetFill(0, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CF_EXPS_PRICE2), SetMinimalSize(86, 0), SetFill(0, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CF_EXPS_PRICE3), SetMinimalSize(86, 0), SetFill(0, 0),
			EndContainer(),
		EndContainer(),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_HORIZONTAL), SetPadding(WidgetDimensions::unscaled.framerect),
			NWidget(NWID_VERTICAL), // Vertical column with 'bank balance', 'loan'
				NWidget(WWT_TEXT, COLOUR_GREY), SetDataTip(STR_FINANCES_OWN_FUNDS_TITLE, STR_NULL), SetFill(1, 0),
				NWidget(WWT_TEXT, COLOUR_GREY), SetDataTip(STR_FINANCES_LOAN_TITLE, STR_NULL), SetFill(1, 0),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2), SetFill(1, 0),
				NWidget(WWT_TEXT, COLOUR_GREY), SetDataTip(STR_FINANCES_BANK_BALANCE_TITLE, STR_NULL), SetFill(1, 0),
				NWidget(NWID_SPACER), SetFill(0, 1),
			EndContainer(),
			NWidget(NWID_SPACER), SetFill(0, 0), SetMinimalSize(30, 0),
			NWidget(NWID_VERTICAL), // Vertical column with bank balance amount, loan amount, and total.
				NWidget(WWT_TEXT, COLOUR_GREY, WID_CF_OWN_VALUE), SetDataTip(STR_FINANCES_TOTAL_CURRENCY, STR_NULL), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
				NWidget(WWT_TEXT, COLOUR_GREY, WID_CF_LOAN_VALUE), SetDataTip(STR_FINANCES_TOTAL_CURRENCY, STR_NULL), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CF_BALANCE_LINE), SetMinimalSize(0, 2), SetFill(1, 0),
				NWidget(WWT_TEXT, COLOUR_GREY, WID_CF_BALANCE_VALUE), SetDataTip(STR_FINANCES_BANK_BALANCE, STR_NULL), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
			EndContainer(),
			NWidget(NWID_SELECTION, INVALID_COLOUR, WID_CF_SEL_MAXLOAN),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SPACER), SetFill(0, 1), SetMinimalSize(25, 0),
					NWidget(NWID_VERTICAL), // Max loan information
						NWidget(WWT_TEXT, COLOUR_GREY, WID_CF_INTEREST_RATE), SetDataTip(STR_FINANCES_INTEREST_RATE, STR_NULL),
						NWidget(WWT_TEXT, COLOUR_GREY, WID_CF_MAXLOAN_VALUE), SetDataTip(STR_FINANCES_MAX_LOAN, STR_NULL),
						NWidget(NWID_SPACER), SetFill(0, 1),
					EndContainer(),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_SPACER), SetFill(1, 1),
		EndContainer(),
	EndContainer(),
	NWidget(NWID_SELECTION, INVALID_COLOUR, WID_CF_SEL_BUTTONS),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_CF_INCREASE_LOAN), SetFill(1, 0), SetDataTip(STR_FINANCES_BORROW_BUTTON, STR_FINANCES_BORROW_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_CF_REPAY_LOAN), SetFill(1, 0), SetDataTip(STR_FINANCES_REPAY_BUTTON, STR_FINANCES_REPAY_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_CF_INFRASTRUCTURE), SetFill(1, 0), SetDataTip(STR_FINANCES_INFRASTRUCTURE_BUTTON, STR_COMPANY_VIEW_INFRASTRUCTURE_TOOLTIP),
		EndContainer(),
	EndContainer(),
};

/** Window class displaying the company finances. */
struct CompanyFinancesWindow : Window {
	static Money max_money; ///< The maximum amount of money a company has had this 'run'
	bool small;             ///< Window is toggled to 'small'.

	CompanyFinancesWindow(WindowDesc *desc, CompanyID company) : Window(desc)
	{
		this->small = false;
		this->CreateNestedTree();
		this->SetupWidgets();
		this->FinishInitNested(company);

		this->owner = (Owner)this->window_number;
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_CF_CAPTION:
				SetDParam(0, (CompanyID)this->window_number);
				SetDParam(1, (CompanyID)this->window_number);
				break;

			case WID_CF_BALANCE_VALUE: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				SetDParam(0, c->money);
				break;
			}

			case WID_CF_LOAN_VALUE: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				SetDParam(0, c->current_loan);
				break;
			}

			case WID_CF_OWN_VALUE: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				SetDParam(0, c->money - c->current_loan);
				break;
			}

			case WID_CF_INTEREST_RATE:
				SetDParam(0, _settings_game.difficulty.initial_interest);
				break;

			case WID_CF_MAXLOAN_VALUE:
				SetDParam(0, _economy.max_loan);
				break;

			case WID_CF_INCREASE_LOAN:
			case WID_CF_REPAY_LOAN:
				SetDParam(0, LOAN_INTERVAL);
				break;
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_CF_EXPS_CATEGORY:
				size->width  = GetMaxCategoriesWidth();
				size->height = GetTotalCategoriesHeight();
				break;

			case WID_CF_EXPS_PRICE1:
			case WID_CF_EXPS_PRICE2:
			case WID_CF_EXPS_PRICE3:
				size->height = GetTotalCategoriesHeight();
				FALLTHROUGH;

			case WID_CF_BALANCE_VALUE:
			case WID_CF_LOAN_VALUE:
			case WID_CF_OWN_VALUE:
				SetDParamMaxValue(0, CompanyFinancesWindow::max_money);
				size->width = std::max(GetStringBoundingBox(STR_FINANCES_NEGATIVE_INCOME).width, GetStringBoundingBox(STR_FINANCES_POSITIVE_INCOME).width) + padding.width;
				break;

			case WID_CF_INTEREST_RATE:
				size->height = FONT_HEIGHT_NORMAL;
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_CF_EXPS_CATEGORY:
				DrawCategories(r);
				break;

			case WID_CF_EXPS_PRICE1:
			case WID_CF_EXPS_PRICE2:
			case WID_CF_EXPS_PRICE3: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				int age = std::min(_cur_year - c->inaugurated_year, 2);
				int wid_offset = widget - WID_CF_EXPS_PRICE1;
				if (wid_offset <= age) {
					DrawYearColumn(r, _cur_year - (age - wid_offset), c->yearly_expenses[age - wid_offset]);
				}
				break;
			}

			case WID_CF_BALANCE_LINE:
				GfxFillRect(r.left, r.top, r.right, r.top, PC_BLACK);
				break;
		}
	}

	/**
	 * Setup the widgets in the nested tree, such that the finances window is displayed properly.
	 * @note After setup, the window must be (re-)initialized.
	 */
	void SetupWidgets()
	{
		int plane = this->small ? SZSP_NONE : 0;
		this->GetWidget<NWidgetStacked>(WID_CF_SEL_PANEL)->SetDisplayedPlane(plane);
		this->GetWidget<NWidgetStacked>(WID_CF_SEL_MAXLOAN)->SetDisplayedPlane(plane);

		CompanyID company = (CompanyID)this->window_number;
		plane = (company != _local_company) ? SZSP_NONE : 0;
		this->GetWidget<NWidgetStacked>(WID_CF_SEL_BUTTONS)->SetDisplayedPlane(plane);
	}

	void OnPaint() override
	{
		if (!this->IsShaded()) {
			if (!this->small) {
				/* Check that the expenses panel height matches the height needed for the layout. */
				if (GetTotalCategoriesHeight() != this->GetWidget<NWidgetBase>(WID_CF_EXPS_CATEGORY)->current_y) {
					this->SetupWidgets();
					this->ReInit();
					return;
				}
			}

			/* Check that the loan buttons are shown only when the user owns the company. */
			CompanyID company = (CompanyID)this->window_number;
			int req_plane = (company != _local_company) ? SZSP_NONE : 0;
			if (req_plane != this->GetWidget<NWidgetStacked>(WID_CF_SEL_BUTTONS)->shown_plane) {
				this->SetupWidgets();
				this->ReInit();
				return;
			}

			const Company *c = Company::Get(company);
			this->SetWidgetDisabledState(WID_CF_INCREASE_LOAN, c->current_loan == _economy.max_loan); // Borrow button only shows when there is any more money to loan.
			this->SetWidgetDisabledState(WID_CF_REPAY_LOAN, company != _local_company || c->current_loan == 0); // Repay button only shows when there is any more money to repay.
		}

		this->DrawWidgets();
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_CF_TOGGLE_SIZE: // toggle size
				this->small = !this->small;
				this->SetupWidgets();
				if (this->IsShaded()) {
					/* Finances window is not resizable, so size hints given during unshading have no effect
					 * on the changed appearance of the window. */
					this->SetShaded(false);
				} else {
					this->ReInit();
				}
				break;

			case WID_CF_INCREASE_LOAN: // increase loan
				Command<CMD_INCREASE_LOAN>::Post(STR_ERROR_CAN_T_BORROW_ANY_MORE_MONEY, _ctrl_pressed ? LoanCommand::Max : LoanCommand::Interval, 0);
				break;

			case WID_CF_REPAY_LOAN: // repay loan
				Command<CMD_DECREASE_LOAN>::Post(STR_ERROR_CAN_T_REPAY_LOAN, _ctrl_pressed ? LoanCommand::Max : LoanCommand::Interval, 0);
				break;

			case WID_CF_INFRASTRUCTURE: // show infrastructure details
				ShowCompanyInfrastructure((CompanyID)this->window_number);
				break;
		}
	}

	void OnHundredthTick() override
	{
		const Company *c = Company::Get((CompanyID)this->window_number);
		if (c->money > CompanyFinancesWindow::max_money) {
			CompanyFinancesWindow::max_money = std::max(c->money * 2, CompanyFinancesWindow::max_money * 4);
			this->SetupWidgets();
			this->ReInit();
		}
	}
};

/** First conservative estimate of the maximum amount of money */
Money CompanyFinancesWindow::max_money = INT32_MAX;

static WindowDesc _company_finances_desc(
	WDP_AUTO, "company_finances", 0, 0,
	WC_FINANCES, WC_NONE,
	0,
	_nested_company_finances_widgets, lengthof(_nested_company_finances_widgets)
);

/**
 * Open the finances window of a company.
 * @param company Company to show finances of.
 * @pre is company a valid company.
 */
void ShowCompanyFinances(CompanyID company)
{
	if (!Company::IsValidID(company)) return;
	if (BringWindowToFrontById(WC_FINANCES, company)) return;

	new CompanyFinancesWindow(&_company_finances_desc, company);
}

/* List of colours for the livery window */
static const StringID _colour_dropdown[] = {
	STR_COLOUR_DARK_BLUE,
	STR_COLOUR_PALE_GREEN,
	STR_COLOUR_PINK,
	STR_COLOUR_YELLOW,
	STR_COLOUR_RED,
	STR_COLOUR_LIGHT_BLUE,
	STR_COLOUR_GREEN,
	STR_COLOUR_DARK_GREEN,
	STR_COLOUR_BLUE,
	STR_COLOUR_CREAM,
	STR_COLOUR_MAUVE,
	STR_COLOUR_PURPLE,
	STR_COLOUR_ORANGE,
	STR_COLOUR_BROWN,
	STR_COLOUR_GREY,
	STR_COLOUR_WHITE,
};

/* Association of liveries to livery classes */
static const LiveryClass _livery_class[LS_END] = {
	LC_OTHER,
	LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL, LC_RAIL,
	LC_ROAD, LC_ROAD,
	LC_SHIP, LC_SHIP,
	LC_AIRCRAFT, LC_AIRCRAFT, LC_AIRCRAFT,
	LC_ROAD, LC_ROAD,
};

class DropDownListColourItem : public DropDownListItem {
public:
	DropDownListColourItem(int result, bool masked) : DropDownListItem(result, masked) {}

	StringID String() const
	{
		return this->result >= COLOUR_END ? STR_COLOUR_DEFAULT : _colour_dropdown[this->result];
	}

	uint Height(uint width) const override
	{
		return std::max(FONT_HEIGHT_NORMAL, ScaleGUITrad(12) + 2);
	}

	bool Selectable() const override
	{
		return true;
	}

	void Draw(const Rect &r, bool sel, Colours bg_colour) const override
	{
		bool rtl = _current_text_dir == TD_RTL;
		int icon_y = CenterBounds(r.top, r.bottom, 0);
		int text_y = CenterBounds(r.top, r.bottom, FONT_HEIGHT_NORMAL);
		Rect tr = r.Shrink(WidgetDimensions::scaled.dropdowntext);
		DrawSprite(SPR_VEH_BUS_SIDE_VIEW, PALETTE_RECOLOUR_START + (this->result % COLOUR_END),
				   rtl ? tr.right - ScaleGUITrad(14) : tr.left + ScaleGUITrad(14),
				   icon_y);
		tr = tr.Indent(ScaleGUITrad(28) + WidgetDimensions::scaled.hsep_normal, rtl);
		DrawString(tr.left, tr.right, text_y, this->String(), sel ? TC_WHITE : TC_BLACK);
	}
};

typedef GUIList<const Group*> GUIGroupList;

/** Company livery colour scheme window. */
struct SelectCompanyLiveryWindow : public Window {
private:
	uint32 sel;
	LiveryClass livery_class;
	Dimension square;
	uint rows;
	uint line_height;
	GUIGroupList groups;
	std::vector<int> indents;
	Scrollbar *vscroll;

	void ShowColourDropDownMenu(uint32 widget)
	{
		uint32 used_colours = 0;
		const Livery *livery, *default_livery = nullptr;
		bool primary = widget == WID_SCL_PRI_COL_DROPDOWN;
		byte default_col = 0;

		/* Disallow other company colours for the primary colour */
		if (this->livery_class < LC_GROUP_RAIL && HasBit(this->sel, LS_DEFAULT) && primary) {
			for (const Company *c : Company::Iterate()) {
				if (c->index != _local_company) SetBit(used_colours, c->colour);
			}
		}

		const Company *c = Company::Get((CompanyID)this->window_number);

		if (this->livery_class < LC_GROUP_RAIL) {
			/* Get the first selected livery to use as the default dropdown item */
			LiveryScheme scheme;
			for (scheme = LS_BEGIN; scheme < LS_END; scheme++) {
				if (HasBit(this->sel, scheme)) break;
			}
			if (scheme == LS_END) scheme = LS_DEFAULT;
			livery = &c->livery[scheme];
			if (scheme != LS_DEFAULT) default_livery = &c->livery[LS_DEFAULT];
		} else {
			const Group *g = Group::Get(this->sel);
			livery = &g->livery;
			if (g->parent == INVALID_GROUP) {
				default_livery = &c->livery[LS_DEFAULT];
			} else {
				const Group *pg = Group::Get(g->parent);
				default_livery = &pg->livery;
			}
		}

		DropDownList list;
		if (default_livery != nullptr) {
			/* Add COLOUR_END to put the colour out of range, but also allow us to show what the default is */
			default_col = (primary ? default_livery->colour1 : default_livery->colour2) + COLOUR_END;
			list.emplace_back(new DropDownListColourItem(default_col, false));
		}
		for (uint i = 0; i < lengthof(_colour_dropdown); i++) {
			list.emplace_back(new DropDownListColourItem(i, HasBit(used_colours, i)));
		}

		byte sel = (default_livery == nullptr || HasBit(livery->in_use, primary ? 0 : 1)) ? (primary ? livery->colour1 : livery->colour2) : default_col;
		ShowDropDownList(this, std::move(list), sel, widget);
	}

	void AddChildren(GUIGroupList *source, GroupID parent, int indent)
	{
		for (const Group *g : *source) {
			if (g->parent != parent) continue;
			this->groups.push_back(g);
			this->indents.push_back(indent);
			AddChildren(source, g->index, indent + 1);
		}
	}

	void BuildGroupList(CompanyID owner)
	{
		if (!this->groups.NeedRebuild()) return;

		this->groups.clear();
		this->indents.clear();

		if (this->livery_class >= LC_GROUP_RAIL) {
			GUIGroupList list;
			VehicleType vtype = (VehicleType)(this->livery_class - LC_GROUP_RAIL);

			for (const Group *g : Group::Iterate()) {
				if (g->owner == owner && g->vehicle_type == vtype) {
					list.push_back(g);
				}
			}

			list.ForceResort();

			/* Sort the groups by their name */
			const Group *last_group[2] = { nullptr, nullptr };
			char         last_name[2][64] = { "", "" };
			list.Sort([&](const Group * const &a, const Group * const &b) -> bool {
				if (a != last_group[0]) {
					last_group[0] = a;
					SetDParam(0, a->index);
					GetString(last_name[0], STR_GROUP_NAME, lastof(last_name[0]));
				}

				if (b != last_group[1]) {
					last_group[1] = b;
					SetDParam(0, b->index);
					GetString(last_name[1], STR_GROUP_NAME, lastof(last_name[1]));
				}

				int r = strnatcmp(last_name[0], last_name[1]); // Sort by name (natural sorting).
				if (r == 0) return a->index < b->index;
				return r < 0;
			});

			AddChildren(&list, INVALID_GROUP, 0);
		}

		this->groups.shrink_to_fit();
		this->groups.RebuildDone();
	}

	void SetRows()
	{
		if (this->livery_class < LC_GROUP_RAIL) {
			this->rows = 0;
			for (LiveryScheme scheme = LS_DEFAULT; scheme < LS_END; scheme++) {
				if (_livery_class[scheme] == this->livery_class && HasBit(_loaded_newgrf_features.used_liveries, scheme)) {
					this->rows++;
				}
			}
		} else {
			this->rows = (uint)this->groups.size();
		}

		this->vscroll->SetCount(this->rows);
	}

public:
	SelectCompanyLiveryWindow(WindowDesc *desc, CompanyID company, GroupID group) : Window(desc)
	{
		this->CreateNestedTree();
		this->vscroll = this->GetScrollbar(WID_SCL_MATRIX_SCROLLBAR);

		if (group == INVALID_GROUP) {
			this->livery_class = LC_OTHER;
			this->sel = 1;
			this->LowerWidget(WID_SCL_CLASS_GENERAL);
			this->BuildGroupList(company);
			this->SetRows();
		} else {
			this->SetSelectedGroup(company, group);
		}

		this->FinishInitNested(company);
		this->owner = company;
		this->InvalidateData(1);
	}

	void SetSelectedGroup(CompanyID company, GroupID group)
	{
		this->RaiseWidget(this->livery_class + WID_SCL_CLASS_GENERAL);
		const Group *g = Group::Get(group);
		switch (g->vehicle_type) {
			case VEH_TRAIN: this->livery_class = LC_GROUP_RAIL; break;
			case VEH_ROAD: this->livery_class = LC_GROUP_ROAD; break;
			case VEH_SHIP: this->livery_class = LC_GROUP_SHIP; break;
			case VEH_AIRCRAFT: this->livery_class = LC_GROUP_AIRCRAFT; break;
			default: NOT_REACHED();
		}
		this->sel = group;
		this->LowerWidget(this->livery_class + WID_SCL_CLASS_GENERAL);

		this->groups.ForceRebuild();
		this->BuildGroupList(company);
		this->SetRows();

		/* Position scrollbar to selected group */
		for (uint i = 0; i < this->rows; i++) {
			if (this->groups[i]->index == sel) {
				this->vscroll->SetPosition(Clamp(i - this->vscroll->GetCapacity() / 2, 0, std::max(this->vscroll->GetCount() - this->vscroll->GetCapacity(), 0)));
				break;
			}
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_SCL_SPACER_DROPDOWN: {
				/* The matrix widget below needs enough room to print all the schemes. */
				Dimension d = {0, 0};
				for (LiveryScheme scheme = LS_DEFAULT; scheme < LS_END; scheme++) {
					d = maxdim(d, GetStringBoundingBox(STR_LIVERY_DEFAULT + scheme));
				}

				/* And group names */
				for (const Group *g : Group::Iterate()) {
					if (g->owner == (CompanyID)this->window_number) {
						SetDParam(0, g->index);
						d = maxdim(d, GetStringBoundingBox(STR_GROUP_NAME));
					}
				}

				size->width = std::max(size->width, 5 + d.width + padding.width);
				break;
			}

			case WID_SCL_MATRIX: {
				/* 11 items in the default rail class */
				this->square = GetSpriteSize(SPR_SQUARE);
				this->line_height = std::max(this->square.height, (uint)FONT_HEIGHT_NORMAL) + padding.height;

				size->height = 11 * this->line_height;
				resize->width = 1;
				resize->height = this->line_height;
				break;
			}

			case WID_SCL_SEC_COL_DROPDOWN:
				if (!_loaded_newgrf_features.has_2CC) {
					size->width = 0;
					break;
				}
				FALLTHROUGH;

			case WID_SCL_PRI_COL_DROPDOWN: {
				this->square = GetSpriteSize(SPR_SQUARE);
				int string_padding = this->square.width + WidgetDimensions::scaled.hsep_normal + padding.width;
				for (const StringID *id = _colour_dropdown; id != endof(_colour_dropdown); id++) {
					size->width = std::max(size->width, GetStringBoundingBox(*id).width + string_padding);
				}
				size->width = std::max(size->width, GetStringBoundingBox(STR_COLOUR_DEFAULT).width + string_padding);
				break;
			}
		}
	}

	void OnPaint() override
	{
		bool local = (CompanyID)this->window_number == _local_company;

		/* Disable dropdown controls if no scheme is selected */
		bool disabled = this->livery_class < LC_GROUP_RAIL ? (this->sel == 0) : (this->sel == INVALID_GROUP);
		this->SetWidgetDisabledState(WID_SCL_PRI_COL_DROPDOWN, !local || disabled);
		this->SetWidgetDisabledState(WID_SCL_SEC_COL_DROPDOWN, !local || disabled);

		this->BuildGroupList((CompanyID)this->window_number);

		this->DrawWidgets();
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_SCL_CAPTION:
				SetDParam(0, (CompanyID)this->window_number);
				break;

			case WID_SCL_PRI_COL_DROPDOWN:
			case WID_SCL_SEC_COL_DROPDOWN: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				bool primary = widget == WID_SCL_PRI_COL_DROPDOWN;
				StringID colour = STR_COLOUR_DEFAULT;

				if (this->livery_class < LC_GROUP_RAIL) {
					if (this->sel != 0) {
						LiveryScheme scheme = LS_DEFAULT;
						for (scheme = LS_BEGIN; scheme < LS_END; scheme++) {
							if (HasBit(this->sel, scheme)) break;
						}
						if (scheme == LS_END) scheme = LS_DEFAULT;
						const Livery *livery = &c->livery[scheme];
						if (scheme == LS_DEFAULT || HasBit(livery->in_use, primary ? 0 : 1)) {
							colour = STR_COLOUR_DARK_BLUE + (primary ? livery->colour1 : livery->colour2);
						}
					}
				} else {
					if (this->sel != INVALID_GROUP) {
						const Group *g = Group::Get(this->sel);
						const Livery *livery = &g->livery;
						if (HasBit(livery->in_use, primary ? 0 : 1)) {
							colour = STR_COLOUR_DARK_BLUE + (primary ? livery->colour1 : livery->colour2);
						}
					}
				}
				SetDParam(0, colour);
				break;
			}
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		if (widget != WID_SCL_MATRIX) return;

		bool rtl = _current_text_dir == TD_RTL;

		/* Coordinates of scheme name column. */
		const NWidgetBase *nwi = this->GetWidget<NWidgetBase>(WID_SCL_SPACER_DROPDOWN);
		Rect sch = nwi->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect);
		/* Coordinates of first dropdown. */
		nwi = this->GetWidget<NWidgetBase>(WID_SCL_PRI_COL_DROPDOWN);
		Rect pri = nwi->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect);
		/* Coordinates of second dropdown. */
		nwi = this->GetWidget<NWidgetBase>(WID_SCL_SEC_COL_DROPDOWN);
		Rect sec = nwi->GetCurrentRect().Shrink(WidgetDimensions::scaled.framerect);

		Rect pri_squ = pri.WithWidth(this->square.width, rtl);
		Rect sec_squ = sec.WithWidth(this->square.width, rtl);

		pri = pri.Indent(this->square.width + WidgetDimensions::scaled.hsep_normal, rtl);
		sec = sec.Indent(this->square.width + WidgetDimensions::scaled.hsep_normal, rtl);

		Rect ir = r.WithHeight(this->resize.step_height).Shrink(WidgetDimensions::scaled.matrix);
		int square_offs = (ir.Height() - this->square.height) / 2;
		int text_offs   = (ir.Height() - FONT_HEIGHT_NORMAL) / 2;

		int y = ir.top;

		/* Helper function to draw livery info. */
		auto draw_livery = [&](StringID str, const Livery &liv, bool sel, bool def, int indent) {
			/* Livery Label. */
			DrawString(sch.left + (rtl ? 0 : indent), sch.right - (rtl ? indent : 0), y + text_offs, str, sel ? TC_WHITE : TC_BLACK);

			/* Text below the first dropdown. */
			DrawSprite(SPR_SQUARE, GENERAL_SPRITE_COLOUR(liv.colour1), pri_squ.left, y + square_offs);
			DrawString(pri.left, pri.right, y + text_offs, (def || HasBit(liv.in_use, 0)) ? STR_COLOUR_DARK_BLUE + liv.colour1 : STR_COLOUR_DEFAULT, sel ? TC_WHITE : TC_GOLD);

			/* Text below the second dropdown. */
			if (sec.right > sec.left) { // Second dropdown has non-zero size.
				DrawSprite(SPR_SQUARE, GENERAL_SPRITE_COLOUR(liv.colour2), sec_squ.left, y + square_offs);
				DrawString(sec.left, sec.right, y + text_offs, (def || HasBit(liv.in_use, 1)) ? STR_COLOUR_DARK_BLUE + liv.colour2 : STR_COLOUR_DEFAULT, sel ? TC_WHITE : TC_GOLD);
			}

			y += this->line_height;
		};

		if (livery_class < LC_GROUP_RAIL) {
			int pos = this->vscroll->GetPosition();
			const Company *c = Company::Get((CompanyID)this->window_number);
			for (LiveryScheme scheme = LS_DEFAULT; scheme < LS_END; scheme++) {
				if (_livery_class[scheme] == this->livery_class && HasBit(_loaded_newgrf_features.used_liveries, scheme)) {
					if (pos-- > 0) continue;
					draw_livery(STR_LIVERY_DEFAULT + scheme, c->livery[scheme], HasBit(this->sel, scheme), scheme == LS_DEFAULT, 0);
				}
			}
		} else {
			uint max = static_cast<uint>(std::min<size_t>(this->vscroll->GetPosition() + this->vscroll->GetCapacity(), this->groups.size()));
			for (uint i = this->vscroll->GetPosition(); i < max; ++i) {
				const Group *g = this->groups[i];
				SetDParam(0, g->index);
				draw_livery(STR_GROUP_NAME, g->livery, this->sel == g->index, false, this->indents[i] * WidgetDimensions::scaled.hsep_indent);
			}
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			/* Livery Class buttons */
			case WID_SCL_CLASS_GENERAL:
			case WID_SCL_CLASS_RAIL:
			case WID_SCL_CLASS_ROAD:
			case WID_SCL_CLASS_SHIP:
			case WID_SCL_CLASS_AIRCRAFT:
			case WID_SCL_GROUPS_RAIL:
			case WID_SCL_GROUPS_ROAD:
			case WID_SCL_GROUPS_SHIP:
			case WID_SCL_GROUPS_AIRCRAFT:
				this->RaiseWidget(this->livery_class + WID_SCL_CLASS_GENERAL);
				this->livery_class = (LiveryClass)(widget - WID_SCL_CLASS_GENERAL);
				this->LowerWidget(this->livery_class + WID_SCL_CLASS_GENERAL);

				/* Select the first item in the list */
				if (this->livery_class < LC_GROUP_RAIL) {
					this->sel = 0;
					for (LiveryScheme scheme = LS_DEFAULT; scheme < LS_END; scheme++) {
						if (_livery_class[scheme] == this->livery_class && HasBit(_loaded_newgrf_features.used_liveries, scheme)) {
							this->sel = 1 << scheme;
							break;
						}
					}
				} else {
					this->sel = INVALID_GROUP;
					this->groups.ForceRebuild();
					this->BuildGroupList((CompanyID)this->window_number);

					if (this->groups.size() > 0) {
						this->sel = this->groups[0]->index;
					}
				}

				this->SetRows();
				this->SetDirty();
				break;

			case WID_SCL_PRI_COL_DROPDOWN: // First colour dropdown
				ShowColourDropDownMenu(WID_SCL_PRI_COL_DROPDOWN);
				break;

			case WID_SCL_SEC_COL_DROPDOWN: // Second colour dropdown
				ShowColourDropDownMenu(WID_SCL_SEC_COL_DROPDOWN);
				break;

			case WID_SCL_MATRIX: {
				uint row = this->vscroll->GetScrolledRowFromWidget(pt.y, this, WID_SCL_MATRIX);
				if (row >= this->rows) return;

				if (this->livery_class < LC_GROUP_RAIL) {
					LiveryScheme j = (LiveryScheme)row;

					for (LiveryScheme scheme = LS_BEGIN; scheme <= j && scheme < LS_END; scheme++) {
						if (_livery_class[scheme] != this->livery_class || !HasBit(_loaded_newgrf_features.used_liveries, scheme)) j++;
					}
					assert(j < LS_END);

					if (_ctrl_pressed) {
						ToggleBit(this->sel, j);
					} else {
						this->sel = 1 << j;
					}
				} else {
					this->sel = this->groups[row]->index;
				}
				this->SetDirty();
				break;
			}
		}
	}

	void OnResize() override
	{
		this->vscroll->SetCapacityFromWidget(this, WID_SCL_MATRIX);
	}

	void OnDropdownSelect(int widget, int index) override
	{
		bool local = (CompanyID)this->window_number == _local_company;
		if (!local) return;

		if (index >= COLOUR_END) index = INVALID_COLOUR;

		if (this->livery_class < LC_GROUP_RAIL) {
			/* Set company colour livery */
			for (LiveryScheme scheme = LS_DEFAULT; scheme < LS_END; scheme++) {
				/* Changed colour for the selected scheme, or all visible schemes if CTRL is pressed. */
				if (HasBit(this->sel, scheme) || (_ctrl_pressed && _livery_class[scheme] == this->livery_class && HasBit(_loaded_newgrf_features.used_liveries, scheme))) {
					Command<CMD_SET_COMPANY_COLOUR>::Post(scheme, widget == WID_SCL_PRI_COL_DROPDOWN, (Colours)index);
				}
			}
		} else {
			/* Setting group livery */
			Command<CMD_SET_GROUP_LIVERY>::Post(this->sel, widget == WID_SCL_PRI_COL_DROPDOWN, (Colours)index);
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;

		if (data != -1) {
			/* data contains a VehicleType, rebuild list if it displayed */
			if (this->livery_class == data + LC_GROUP_RAIL) {
				this->groups.ForceRebuild();
				this->BuildGroupList((CompanyID)this->window_number);
				this->SetRows();

				if (!Group::IsValidID(this->sel)) {
					this->sel = INVALID_GROUP;
					if (this->groups.size() > 0) this->sel = this->groups[0]->index;
				}

				this->SetDirty();
			}
			return;
		}

		this->SetWidgetsDisabledState(true, WID_SCL_CLASS_RAIL, WID_SCL_CLASS_ROAD, WID_SCL_CLASS_SHIP, WID_SCL_CLASS_AIRCRAFT, WIDGET_LIST_END);

		bool current_class_valid = this->livery_class == LC_OTHER || this->livery_class >= LC_GROUP_RAIL;
		if (_settings_client.gui.liveries == LIT_ALL || (_settings_client.gui.liveries == LIT_COMPANY && this->window_number == _local_company)) {
			for (LiveryScheme scheme = LS_DEFAULT; scheme < LS_END; scheme++) {
				if (HasBit(_loaded_newgrf_features.used_liveries, scheme)) {
					if (_livery_class[scheme] == this->livery_class) current_class_valid = true;
					this->EnableWidget(WID_SCL_CLASS_GENERAL + _livery_class[scheme]);
				} else if (this->livery_class < LC_GROUP_RAIL) {
					ClrBit(this->sel, scheme);
				}
			}
		}

		if (!current_class_valid) {
			Point pt = {0, 0};
			this->OnClick(pt, WID_SCL_CLASS_GENERAL, 1);
		}
	}
};

static const NWidgetPart _nested_select_company_livery_widgets [] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_SCL_CAPTION), SetDataTip(STR_LIVERY_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_SCL_CLASS_GENERAL), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_COMPANY_GENERAL, STR_LIVERY_GENERAL_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_SCL_CLASS_RAIL), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_TRAINLIST, STR_LIVERY_TRAIN_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_SCL_CLASS_ROAD), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_TRUCKLIST, STR_LIVERY_ROAD_VEHICLE_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_SCL_CLASS_SHIP), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_SHIPLIST, STR_LIVERY_SHIP_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_SCL_CLASS_AIRCRAFT), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_IMG_AIRPLANESLIST, STR_LIVERY_AIRCRAFT_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_SCL_GROUPS_RAIL), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_GROUP_LIVERY_TRAIN, STR_LIVERY_TRAIN_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_SCL_GROUPS_ROAD), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_GROUP_LIVERY_ROADVEH, STR_LIVERY_ROAD_VEHICLE_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_SCL_GROUPS_SHIP), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_GROUP_LIVERY_SHIP, STR_LIVERY_SHIP_TOOLTIP),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_SCL_GROUPS_AIRCRAFT), SetMinimalSize(22, 22), SetFill(0, 1), SetDataTip(SPR_GROUP_LIVERY_AIRCRAFT, STR_LIVERY_AIRCRAFT_TOOLTIP),
		NWidget(WWT_PANEL, COLOUR_GREY), SetMinimalSize(90, 22), SetFill(1, 1), EndContainer(),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_PANEL, COLOUR_GREY, WID_SCL_SPACER_DROPDOWN), SetMinimalSize(150, 12), SetFill(1, 1), EndContainer(),
		NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_SCL_PRI_COL_DROPDOWN), SetMinimalSize(125, 12), SetFill(0, 1), SetDataTip(STR_BLACK_STRING, STR_LIVERY_PRIMARY_TOOLTIP),
		NWidget(WWT_DROPDOWN, COLOUR_GREY, WID_SCL_SEC_COL_DROPDOWN), SetMinimalSize(125, 12), SetFill(0, 1),
				SetDataTip(STR_BLACK_STRING, STR_LIVERY_SECONDARY_TOOLTIP),
	EndContainer(),
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_MATRIX, COLOUR_GREY, WID_SCL_MATRIX), SetMinimalSize(275, 0), SetResize(1, 0), SetFill(1, 1), SetMatrixDataTip(1, 0, STR_LIVERY_PANEL_TOOLTIP), SetScrollbar(WID_SCL_MATRIX_SCROLLBAR),
		NWidget(NWID_VERTICAL),
			NWidget(NWID_VSCROLLBAR, COLOUR_GREY, WID_SCL_MATRIX_SCROLLBAR),
			NWidget(WWT_RESIZEBOX, COLOUR_GREY),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _select_company_livery_desc(
	WDP_AUTO, "company_livery", 0, 0,
	WC_COMPANY_COLOUR, WC_NONE,
	0,
	_nested_select_company_livery_widgets, lengthof(_nested_select_company_livery_widgets)
);

void ShowCompanyLiveryWindow(CompanyID company, GroupID group)
{
	SelectCompanyLiveryWindow *w = (SelectCompanyLiveryWindow *)BringWindowToFrontById(WC_COMPANY_COLOUR, company);
	if (w == nullptr) {
		new SelectCompanyLiveryWindow(&_select_company_livery_desc, company, group);
	} else if (group != INVALID_GROUP) {
		w->SetSelectedGroup(company, group);
	}
}

/**
 * Draws the face of a company manager's face.
 * @param cmf   the company manager's face
 * @param colour the (background) colour of the gradient
 * @param x     x-position to draw the face
 * @param y     y-position to draw the face
 */
void DrawCompanyManagerFace(CompanyManagerFace cmf, int colour, int x, int y)
{
	GenderEthnicity ge = (GenderEthnicity)GetCompanyManagerFaceBits(cmf, CMFV_GEN_ETHN, GE_WM);

	bool has_moustache   = !HasBit(ge, GENDER_FEMALE) && GetCompanyManagerFaceBits(cmf, CMFV_HAS_MOUSTACHE,   ge) != 0;
	bool has_tie_earring = !HasBit(ge, GENDER_FEMALE) || GetCompanyManagerFaceBits(cmf, CMFV_HAS_TIE_EARRING, ge) != 0;
	bool has_glasses     = GetCompanyManagerFaceBits(cmf, CMFV_HAS_GLASSES, ge) != 0;
	PaletteID pal;

	/* Modify eye colour palette only if 2 or more valid values exist */
	if (_cmf_info[CMFV_EYE_COLOUR].valid_values[ge] < 2) {
		pal = PAL_NONE;
	} else {
		switch (GetCompanyManagerFaceBits(cmf, CMFV_EYE_COLOUR, ge)) {
			default: NOT_REACHED();
			case 0: pal = PALETTE_TO_BROWN; break;
			case 1: pal = PALETTE_TO_BLUE;  break;
			case 2: pal = PALETTE_TO_GREEN; break;
		}
	}

	/* Draw the gradient (background) */
	DrawSprite(SPR_GRADIENT, GENERAL_SPRITE_COLOUR(colour), x, y);

	for (CompanyManagerFaceVariable cmfv = CMFV_CHEEKS; cmfv < CMFV_END; cmfv++) {
		switch (cmfv) {
			case CMFV_MOUSTACHE:   if (!has_moustache)   continue; break;
			case CMFV_LIPS:
			case CMFV_NOSE:        if (has_moustache)    continue; break;
			case CMFV_TIE_EARRING: if (!has_tie_earring) continue; break;
			case CMFV_GLASSES:     if (!has_glasses)     continue; break;
			default: break;
		}
		DrawSprite(GetCompanyManagerFaceSprite(cmf, cmfv, ge), (cmfv == CMFV_EYEBROWS) ? pal : PAL_NONE, x, y);
	}
}

/** Nested widget description for the company manager face selection dialog */
static const NWidgetPart _nested_select_company_manager_face_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_SCMF_CAPTION), SetDataTip(STR_FACE_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_IMGBTN, COLOUR_GREY, WID_SCMF_TOGGLE_LARGE_SMALL), SetDataTip(SPR_LARGE_SMALL_WINDOW, STR_FACE_ADVANCED_TOOLTIP),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY, WID_SCMF_SELECT_FACE),
		NWidget(NWID_SPACER), SetMinimalSize(0, 2),
		NWidget(NWID_HORIZONTAL), SetPIP(2, 2, 2),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SPACER), SetFill(1, 0),
					NWidget(WWT_EMPTY, COLOUR_GREY, WID_SCMF_FACE), SetMinimalSize(92, 119),
					NWidget(NWID_SPACER), SetFill(1, 0),
				EndContainer(),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_RANDOM_NEW_FACE), SetFill(1, 0), SetDataTip(STR_FACE_NEW_FACE_BUTTON, STR_FACE_NEW_FACE_TOOLTIP),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_SCMF_SEL_LOADSAVE), // Load/number/save buttons under the portrait in the advanced view.
					NWidget(NWID_VERTICAL),
						NWidget(NWID_SPACER), SetMinimalSize(0, 5), SetFill(0, 1),
						NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_LOAD), SetFill(1, 0), SetDataTip(STR_FACE_LOAD, STR_FACE_LOAD_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_FACECODE), SetFill(1, 0), SetDataTip(STR_FACE_FACECODE, STR_FACE_FACECODE_TOOLTIP),
						NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_SAVE), SetFill(1, 0), SetDataTip(STR_FACE_SAVE, STR_FACE_SAVE_TOOLTIP),
						NWidget(NWID_SPACER), SetMinimalSize(0, 5), SetFill(0, 1),
					EndContainer(),
				EndContainer(),
			EndContainer(),
			NWidget(NWID_VERTICAL),
				NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_TOGGLE_LARGE_SMALL_BUTTON), SetFill(1, 0), SetDataTip(STR_FACE_ADVANCED, STR_FACE_ADVANCED_TOOLTIP),
				NWidget(NWID_SPACER), SetMinimalSize(0, 2),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_SCMF_SEL_MALEFEMALE), // Simple male/female face setting.
					NWidget(NWID_VERTICAL),
						NWidget(NWID_SPACER), SetFill(0, 1),
						NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SCMF_MALE), SetFill(1, 0), SetDataTip(STR_FACE_MALE_BUTTON, STR_FACE_MALE_TOOLTIP),
						NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SCMF_FEMALE), SetFill(1, 0), SetDataTip(STR_FACE_FEMALE_BUTTON, STR_FACE_FEMALE_TOOLTIP),
						NWidget(NWID_SPACER), SetFill(0, 1),
					EndContainer(),
				EndContainer(),
				NWidget(NWID_SELECTION, INVALID_COLOUR, WID_SCMF_SEL_PARTS), // Advanced face parts setting.
					NWidget(NWID_VERTICAL),
						NWidget(NWID_SPACER), SetMinimalSize(0, 2),
						NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SCMF_MALE2), SetFill(1, 0), SetDataTip(STR_FACE_MALE_BUTTON, STR_FACE_MALE_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SCMF_FEMALE2), SetFill(1, 0), SetDataTip(STR_FACE_FEMALE_BUTTON, STR_FACE_FEMALE_TOOLTIP),
						EndContainer(),
						NWidget(NWID_SPACER), SetMinimalSize(0, 2),
						NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SCMF_ETHNICITY_EUR), SetFill(1, 0), SetDataTip(STR_FACE_EUROPEAN, STR_FACE_SELECT_EUROPEAN),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_SCMF_ETHNICITY_AFR), SetFill(1, 0), SetDataTip(STR_FACE_AFRICAN, STR_FACE_SELECT_AFRICAN),
						EndContainer(),
						NWidget(NWID_SPACER), SetMinimalSize(0, 4),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCMF_HAS_MOUSTACHE_EARRING_TEXT), SetFill(1, 0), SetPadding(WidgetDimensions::unscaled.framerect),
								SetDataTip(STR_FACE_EYECOLOUR, STR_NULL), SetTextColour(TC_GOLD), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_HAS_MOUSTACHE_EARRING), SetDataTip(STR_WHITE_STRING, STR_FACE_MOUSTACHE_EARRING_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCMF_HAS_GLASSES_TEXT), SetFill(1, 0), SetPadding(WidgetDimensions::unscaled.framerect),
								SetDataTip(STR_FACE_GLASSES, STR_NULL), SetTextColour(TC_GOLD), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_HAS_GLASSES), SetDataTip(STR_WHITE_STRING, STR_FACE_GLASSES_TOOLTIP),
						EndContainer(),
						NWidget(NWID_SPACER), SetMinimalSize(0, 2), SetFill(1, 0),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCMF_HAIR_TEXT), SetFill(1, 0), SetPadding(WidgetDimensions::unscaled.framerect),
								SetDataTip(STR_FACE_HAIR, STR_NULL), SetTextColour(TC_GOLD), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_HAIR_L), SetDataTip(AWV_DECREASE, STR_FACE_HAIR_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_HAIR), SetDataTip(STR_WHITE_STRING, STR_FACE_HAIR_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_HAIR_R), SetDataTip(AWV_INCREASE, STR_FACE_HAIR_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCMF_EYEBROWS_TEXT), SetFill(1, 0), SetPadding(WidgetDimensions::unscaled.framerect),
								SetDataTip(STR_FACE_EYEBROWS, STR_NULL), SetTextColour(TC_GOLD), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_EYEBROWS_L), SetDataTip(AWV_DECREASE, STR_FACE_EYEBROWS_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_EYEBROWS), SetDataTip(STR_WHITE_STRING, STR_FACE_EYEBROWS_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_EYEBROWS_R), SetDataTip(AWV_INCREASE, STR_FACE_EYEBROWS_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCMF_EYECOLOUR_TEXT), SetFill(1, 0), SetPadding(WidgetDimensions::unscaled.framerect),
								SetDataTip(STR_FACE_EYECOLOUR, STR_NULL), SetTextColour(TC_GOLD), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_EYECOLOUR_L), SetDataTip(AWV_DECREASE, STR_FACE_EYECOLOUR_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_EYECOLOUR), SetDataTip(STR_WHITE_STRING, STR_FACE_EYECOLOUR_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_EYECOLOUR_R), SetDataTip(AWV_INCREASE, STR_FACE_EYECOLOUR_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCMF_GLASSES_TEXT), SetFill(1, 0), SetPadding(WidgetDimensions::unscaled.framerect),
								SetDataTip(STR_FACE_GLASSES, STR_NULL), SetTextColour(TC_GOLD), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_GLASSES_L), SetDataTip(AWV_DECREASE, STR_FACE_GLASSES_TOOLTIP_2),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_GLASSES), SetDataTip(STR_WHITE_STRING, STR_FACE_GLASSES_TOOLTIP_2),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_GLASSES_R), SetDataTip(AWV_INCREASE, STR_FACE_GLASSES_TOOLTIP_2),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCMF_NOSE_TEXT), SetFill(1, 0),  SetPadding(WidgetDimensions::unscaled.framerect),
								SetDataTip(STR_FACE_NOSE, STR_NULL), SetTextColour(TC_GOLD), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_NOSE_L), SetDataTip(AWV_DECREASE, STR_FACE_NOSE_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_NOSE), SetDataTip(STR_WHITE_STRING, STR_FACE_NOSE_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_NOSE_R), SetDataTip(AWV_INCREASE, STR_FACE_NOSE_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCMF_LIPS_MOUSTACHE_TEXT), SetFill(1, 0), SetPadding(WidgetDimensions::unscaled.framerect),
								SetDataTip(STR_FACE_MOUSTACHE, STR_NULL), SetTextColour(TC_GOLD), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_LIPS_MOUSTACHE_L), SetDataTip(AWV_DECREASE, STR_FACE_LIPS_MOUSTACHE_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_LIPS_MOUSTACHE), SetDataTip(STR_WHITE_STRING, STR_FACE_LIPS_MOUSTACHE_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_LIPS_MOUSTACHE_R), SetDataTip(AWV_INCREASE, STR_FACE_LIPS_MOUSTACHE_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCMF_CHIN_TEXT), SetFill(1, 0), SetPadding(WidgetDimensions::unscaled.framerect),
								SetDataTip(STR_FACE_CHIN, STR_NULL), SetTextColour(TC_GOLD), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_CHIN_L), SetDataTip(AWV_DECREASE, STR_FACE_CHIN_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_CHIN), SetDataTip(STR_WHITE_STRING, STR_FACE_CHIN_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_CHIN_R), SetDataTip(AWV_INCREASE, STR_FACE_CHIN_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCMF_JACKET_TEXT), SetFill(1, 0), SetPadding(WidgetDimensions::unscaled.framerect),
								SetDataTip(STR_FACE_JACKET, STR_NULL), SetTextColour(TC_GOLD), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_JACKET_L), SetDataTip(AWV_DECREASE, STR_FACE_JACKET_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_JACKET), SetDataTip(STR_WHITE_STRING, STR_FACE_JACKET_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_JACKET_R), SetDataTip(AWV_INCREASE, STR_FACE_JACKET_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCMF_COLLAR_TEXT), SetFill(1, 0), SetPadding(WidgetDimensions::unscaled.framerect),
								SetDataTip(STR_FACE_COLLAR, STR_NULL), SetTextColour(TC_GOLD), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_COLLAR_L), SetDataTip(AWV_DECREASE, STR_FACE_COLLAR_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_COLLAR), SetDataTip(STR_WHITE_STRING, STR_FACE_COLLAR_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_COLLAR_R), SetDataTip(AWV_INCREASE, STR_FACE_COLLAR_TOOLTIP),
						EndContainer(),
						NWidget(NWID_HORIZONTAL),
							NWidget(WWT_TEXT, INVALID_COLOUR, WID_SCMF_TIE_EARRING_TEXT), SetFill(1, 0), SetPadding(WidgetDimensions::unscaled.framerect),
								SetDataTip(STR_FACE_EARRING, STR_NULL), SetTextColour(TC_GOLD), SetAlignment(SA_VERT_CENTER | SA_RIGHT),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_TIE_EARRING_L), SetDataTip(AWV_DECREASE, STR_FACE_TIE_EARRING_TOOLTIP),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_TIE_EARRING), SetDataTip(STR_WHITE_STRING, STR_FACE_TIE_EARRING_TOOLTIP),
							NWidget(WWT_PUSHARROWBTN, COLOUR_GREY, WID_SCMF_TIE_EARRING_R), SetDataTip(AWV_INCREASE, STR_FACE_TIE_EARRING_TOOLTIP),
						EndContainer(),
						NWidget(NWID_SPACER), SetFill(0, 1),
					EndContainer(),
				EndContainer(),
			EndContainer(),
		EndContainer(),
		NWidget(NWID_SPACER), SetMinimalSize(0, 2),
	EndContainer(),
	NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_CANCEL), SetFill(1, 0), SetDataTip(STR_BUTTON_CANCEL, STR_FACE_CANCEL_TOOLTIP),
		NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_SCMF_ACCEPT), SetFill(1, 0), SetDataTip(STR_BUTTON_OK, STR_FACE_OK_TOOLTIP),
	EndContainer(),
};

/** Management class for customizing the face of the company manager. */
class SelectCompanyManagerFaceWindow : public Window
{
	CompanyManagerFace face; ///< company manager face bits
	bool advanced; ///< advanced company manager face selection window

	GenderEthnicity ge; ///< Gender and ethnicity.
	bool is_female;     ///< Female face.
	bool is_moust_male; ///< Male face with a moustache.

	Dimension yesno_dim;  ///< Dimension of a yes/no button of a part in the advanced face window.
	Dimension number_dim; ///< Dimension of a number widget of a part in the advanced face window.

	/**
	 * Set parameters for value of face control buttons.
	 *
	 * @param widget_index   index of this widget in the window
	 * @param val            the value which will be displayed
	 * @param is_bool_widget is it a bool button
	 */
	void SetFaceStringParameters(byte widget_index, uint8 val, bool is_bool_widget) const
	{
		const NWidgetCore *nwi_widget = this->GetWidget<NWidgetCore>(widget_index);
		if (nwi_widget->IsDisabled()) {
			SetDParam(0, STR_EMPTY);
		} else {
			if (is_bool_widget) {
				/* if it a bool button write yes or no */
				SetDParam(0, (val != 0) ? STR_FACE_YES : STR_FACE_NO);
			} else {
				/* else write the value + 1 */
				SetDParam(0, STR_JUST_INT);
				SetDParam(1, val + 1);
			}
		}
	}

	void UpdateData()
	{
		this->ge = (GenderEthnicity)GB(this->face, _cmf_info[CMFV_GEN_ETHN].offset, _cmf_info[CMFV_GEN_ETHN].length); // get the gender and ethnicity
		this->is_female = HasBit(this->ge, GENDER_FEMALE); // get the gender: 0 == male and 1 == female
		this->is_moust_male = !is_female && GetCompanyManagerFaceBits(this->face, CMFV_HAS_MOUSTACHE, this->ge) != 0; // is a male face with moustache

		this->GetWidget<NWidgetCore>(WID_SCMF_HAS_MOUSTACHE_EARRING_TEXT)->widget_data = this->is_female ? STR_FACE_EARRING : STR_FACE_MOUSTACHE;
		this->GetWidget<NWidgetCore>(WID_SCMF_TIE_EARRING_TEXT)->widget_data           = this->is_female ? STR_FACE_EARRING : STR_FACE_TIE;
		this->GetWidget<NWidgetCore>(WID_SCMF_LIPS_MOUSTACHE_TEXT)->widget_data        = this->is_moust_male ? STR_FACE_MOUSTACHE : STR_FACE_LIPS;
	}

public:
	SelectCompanyManagerFaceWindow(WindowDesc *desc, Window *parent) : Window(desc)
	{
		this->advanced = false;
		this->CreateNestedTree();
		this->SelectDisplayPlanes(this->advanced);
		this->FinishInitNested(parent->window_number);
		this->parent = parent;
		this->owner = (Owner)this->window_number;
		this->face = Company::Get((CompanyID)this->window_number)->face;

		this->UpdateData();
	}

	/**
	 * Select planes to display to the user with the #NWID_SELECTION widgets #WID_SCMF_SEL_LOADSAVE, #WID_SCMF_SEL_MALEFEMALE, and #WID_SCMF_SEL_PARTS.
	 * @param advanced Display advanced face management window.
	 */
	void SelectDisplayPlanes(bool advanced)
	{
		this->GetWidget<NWidgetStacked>(WID_SCMF_SEL_LOADSAVE)->SetDisplayedPlane(advanced ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_SCMF_SEL_PARTS)->SetDisplayedPlane(advanced ? 0 : SZSP_NONE);
		this->GetWidget<NWidgetStacked>(WID_SCMF_SEL_MALEFEMALE)->SetDisplayedPlane(advanced ? SZSP_NONE : 0);
		this->GetWidget<NWidgetCore>(WID_SCMF_RANDOM_NEW_FACE)->widget_data = advanced ? STR_FACE_RANDOM : STR_FACE_NEW_FACE_BUTTON;

		NWidgetCore *wi = this->GetWidget<NWidgetCore>(WID_SCMF_TOGGLE_LARGE_SMALL_BUTTON);
		if (advanced) {
			wi->SetDataTip(STR_FACE_SIMPLE, STR_FACE_SIMPLE_TOOLTIP);
		} else {
			wi->SetDataTip(STR_FACE_ADVANCED, STR_FACE_ADVANCED_TOOLTIP);
		}
	}

	void OnInit() override
	{
		/* Size of the boolean yes/no button. */
		Dimension yesno_dim = maxdim(GetStringBoundingBox(STR_FACE_YES), GetStringBoundingBox(STR_FACE_NO));
		yesno_dim.width  += WidgetDimensions::scaled.framerect.Horizontal();
		yesno_dim.height += WidgetDimensions::scaled.framerect.Vertical();
		/* Size of the number button + arrows. */
		Dimension number_dim = {0, 0};
		for (int val = 1; val <= 12; val++) {
			SetDParam(0, val);
			number_dim = maxdim(number_dim, GetStringBoundingBox(STR_JUST_INT));
		}
		uint arrows_width = GetSpriteSize(SPR_ARROW_LEFT).width + GetSpriteSize(SPR_ARROW_RIGHT).width + 2 * (WidgetDimensions::scaled.imgbtn.Horizontal());
		number_dim.width += WidgetDimensions::scaled.framerect.Horizontal() + arrows_width;
		number_dim.height += WidgetDimensions::scaled.framerect.Vertical();
		/* Compute width of both buttons. */
		yesno_dim.width = std::max(yesno_dim.width, number_dim.width);
		number_dim.width = yesno_dim.width - arrows_width;

		this->yesno_dim = yesno_dim;
		this->number_dim = number_dim;
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_SCMF_HAS_MOUSTACHE_EARRING_TEXT:
				*size = maxdim(*size, GetStringBoundingBox(STR_FACE_EARRING));
				*size = maxdim(*size, GetStringBoundingBox(STR_FACE_MOUSTACHE));
				break;

			case WID_SCMF_TIE_EARRING_TEXT:
				*size = maxdim(*size, GetStringBoundingBox(STR_FACE_EARRING));
				*size = maxdim(*size, GetStringBoundingBox(STR_FACE_TIE));
				break;

			case WID_SCMF_LIPS_MOUSTACHE_TEXT:
				*size = maxdim(*size, GetStringBoundingBox(STR_FACE_LIPS));
				*size = maxdim(*size, GetStringBoundingBox(STR_FACE_MOUSTACHE));
				break;

			case WID_SCMF_FACE: {
				Dimension face_size = GetSpriteSize(SPR_GRADIENT);
				size->width  = std::max(size->width,  face_size.width);
				size->height = std::max(size->height, face_size.height);
				break;
			}

			case WID_SCMF_HAS_MOUSTACHE_EARRING:
			case WID_SCMF_HAS_GLASSES:
				*size = this->yesno_dim;
				break;

			case WID_SCMF_EYECOLOUR:
			case WID_SCMF_CHIN:
			case WID_SCMF_EYEBROWS:
			case WID_SCMF_LIPS_MOUSTACHE:
			case WID_SCMF_NOSE:
			case WID_SCMF_HAIR:
			case WID_SCMF_JACKET:
			case WID_SCMF_COLLAR:
			case WID_SCMF_TIE_EARRING:
			case WID_SCMF_GLASSES:
				*size = this->number_dim;
				break;
		}
	}

	void OnPaint() override
	{
		/* lower the non-selected gender button */
		this->SetWidgetsLoweredState(!this->is_female, WID_SCMF_MALE, WID_SCMF_MALE2, WIDGET_LIST_END);
		this->SetWidgetsLoweredState( this->is_female, WID_SCMF_FEMALE, WID_SCMF_FEMALE2, WIDGET_LIST_END);

		/* advanced company manager face selection window */

		/* lower the non-selected ethnicity button */
		this->SetWidgetLoweredState(WID_SCMF_ETHNICITY_EUR, !HasBit(this->ge, ETHNICITY_BLACK));
		this->SetWidgetLoweredState(WID_SCMF_ETHNICITY_AFR,  HasBit(this->ge, ETHNICITY_BLACK));


		/* Disable dynamically the widgets which CompanyManagerFaceVariable has less than 2 options
		 * (or in other words you haven't any choice).
		 * If the widgets depend on a HAS-variable and this is false the widgets will be disabled, too. */

		/* Eye colour buttons */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_EYE_COLOUR].valid_values[this->ge] < 2,
				WID_SCMF_EYECOLOUR, WID_SCMF_EYECOLOUR_L, WID_SCMF_EYECOLOUR_R, WIDGET_LIST_END);

		/* Chin buttons */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_CHIN].valid_values[this->ge] < 2,
				WID_SCMF_CHIN, WID_SCMF_CHIN_L, WID_SCMF_CHIN_R, WIDGET_LIST_END);

		/* Eyebrows buttons */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_EYEBROWS].valid_values[this->ge] < 2,
				WID_SCMF_EYEBROWS, WID_SCMF_EYEBROWS_L, WID_SCMF_EYEBROWS_R, WIDGET_LIST_END);

		/* Lips or (if it a male face with a moustache) moustache buttons */
		this->SetWidgetsDisabledState(_cmf_info[this->is_moust_male ? CMFV_MOUSTACHE : CMFV_LIPS].valid_values[this->ge] < 2,
				WID_SCMF_LIPS_MOUSTACHE, WID_SCMF_LIPS_MOUSTACHE_L, WID_SCMF_LIPS_MOUSTACHE_R, WIDGET_LIST_END);

		/* Nose buttons | male faces with moustache haven't any nose options */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_NOSE].valid_values[this->ge] < 2 || this->is_moust_male,
				WID_SCMF_NOSE, WID_SCMF_NOSE_L, WID_SCMF_NOSE_R, WIDGET_LIST_END);

		/* Hair buttons */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_HAIR].valid_values[this->ge] < 2,
				WID_SCMF_HAIR, WID_SCMF_HAIR_L, WID_SCMF_HAIR_R, WIDGET_LIST_END);

		/* Jacket buttons */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_JACKET].valid_values[this->ge] < 2,
				WID_SCMF_JACKET, WID_SCMF_JACKET_L, WID_SCMF_JACKET_R, WIDGET_LIST_END);

		/* Collar buttons */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_COLLAR].valid_values[this->ge] < 2,
				WID_SCMF_COLLAR, WID_SCMF_COLLAR_L, WID_SCMF_COLLAR_R, WIDGET_LIST_END);

		/* Tie/earring buttons | female faces without earring haven't any earring options */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_TIE_EARRING].valid_values[this->ge] < 2 ||
					(this->is_female && GetCompanyManagerFaceBits(this->face, CMFV_HAS_TIE_EARRING, this->ge) == 0),
				WID_SCMF_TIE_EARRING, WID_SCMF_TIE_EARRING_L, WID_SCMF_TIE_EARRING_R, WIDGET_LIST_END);

		/* Glasses buttons | faces without glasses haven't any glasses options */
		this->SetWidgetsDisabledState(_cmf_info[CMFV_GLASSES].valid_values[this->ge] < 2 || GetCompanyManagerFaceBits(this->face, CMFV_HAS_GLASSES, this->ge) == 0,
				WID_SCMF_GLASSES, WID_SCMF_GLASSES_L, WID_SCMF_GLASSES_R, WIDGET_LIST_END);

		this->DrawWidgets();
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_SCMF_HAS_MOUSTACHE_EARRING:
				if (this->is_female) { // Only for female faces
					this->SetFaceStringParameters(WID_SCMF_HAS_MOUSTACHE_EARRING, GetCompanyManagerFaceBits(this->face, CMFV_HAS_TIE_EARRING, this->ge), true);
				} else { // Only for male faces
					this->SetFaceStringParameters(WID_SCMF_HAS_MOUSTACHE_EARRING, GetCompanyManagerFaceBits(this->face, CMFV_HAS_MOUSTACHE,   this->ge), true);
				}
				break;

			case WID_SCMF_TIE_EARRING:
				this->SetFaceStringParameters(WID_SCMF_TIE_EARRING, GetCompanyManagerFaceBits(this->face, CMFV_TIE_EARRING, this->ge), false);
				break;

			case WID_SCMF_LIPS_MOUSTACHE:
				if (this->is_moust_male) { // Only for male faces with moustache
					this->SetFaceStringParameters(WID_SCMF_LIPS_MOUSTACHE, GetCompanyManagerFaceBits(this->face, CMFV_MOUSTACHE, this->ge), false);
				} else { // Only for female faces or male faces without moustache
					this->SetFaceStringParameters(WID_SCMF_LIPS_MOUSTACHE, GetCompanyManagerFaceBits(this->face, CMFV_LIPS,      this->ge), false);
				}
				break;

			case WID_SCMF_HAS_GLASSES:
				this->SetFaceStringParameters(WID_SCMF_HAS_GLASSES, GetCompanyManagerFaceBits(this->face, CMFV_HAS_GLASSES, this->ge), true );
				break;

			case WID_SCMF_HAIR:
				this->SetFaceStringParameters(WID_SCMF_HAIR,        GetCompanyManagerFaceBits(this->face, CMFV_HAIR,        this->ge), false);
				break;

			case WID_SCMF_EYEBROWS:
				this->SetFaceStringParameters(WID_SCMF_EYEBROWS,    GetCompanyManagerFaceBits(this->face, CMFV_EYEBROWS,    this->ge), false);
				break;

			case WID_SCMF_EYECOLOUR:
				this->SetFaceStringParameters(WID_SCMF_EYECOLOUR,   GetCompanyManagerFaceBits(this->face, CMFV_EYE_COLOUR,  this->ge), false);
				break;

			case WID_SCMF_GLASSES:
				this->SetFaceStringParameters(WID_SCMF_GLASSES,     GetCompanyManagerFaceBits(this->face, CMFV_GLASSES,     this->ge), false);
				break;

			case WID_SCMF_NOSE:
				this->SetFaceStringParameters(WID_SCMF_NOSE,        GetCompanyManagerFaceBits(this->face, CMFV_NOSE,        this->ge), false);
				break;

			case WID_SCMF_CHIN:
				this->SetFaceStringParameters(WID_SCMF_CHIN,        GetCompanyManagerFaceBits(this->face, CMFV_CHIN,        this->ge), false);
				break;

			case WID_SCMF_JACKET:
				this->SetFaceStringParameters(WID_SCMF_JACKET,      GetCompanyManagerFaceBits(this->face, CMFV_JACKET,      this->ge), false);
				break;

			case WID_SCMF_COLLAR:
				this->SetFaceStringParameters(WID_SCMF_COLLAR,      GetCompanyManagerFaceBits(this->face, CMFV_COLLAR,      this->ge), false);
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_SCMF_FACE:
				DrawCompanyManagerFace(this->face, Company::Get((CompanyID)this->window_number)->colour, r.left, r.top);
				break;
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			/* Toggle size, advanced/simple face selection */
			case WID_SCMF_TOGGLE_LARGE_SMALL:
			case WID_SCMF_TOGGLE_LARGE_SMALL_BUTTON:
				this->advanced = !this->advanced;
				this->SelectDisplayPlanes(this->advanced);
				this->ReInit();
				break;

			/* OK button */
			case WID_SCMF_ACCEPT:
				Command<CMD_SET_COMPANY_MANAGER_FACE>::Post(this->face);
				FALLTHROUGH;

			/* Cancel button */
			case WID_SCMF_CANCEL:
				this->Close();
				break;

			/* Load button */
			case WID_SCMF_LOAD:
				this->face = _company_manager_face;
				ScaleAllCompanyManagerFaceBits(this->face);
				ShowErrorMessage(STR_FACE_LOAD_DONE, INVALID_STRING_ID, WL_INFO);
				this->UpdateData();
				this->SetDirty();
				break;

			/* 'Company manager face number' button, view and/or set company manager face number */
			case WID_SCMF_FACECODE:
				SetDParam(0, this->face);
				ShowQueryString(STR_JUST_INT, STR_FACE_FACECODE_CAPTION, 10 + 1, this, CS_NUMERAL, QSF_NONE);
				break;

			/* Save button */
			case WID_SCMF_SAVE:
				_company_manager_face = this->face;
				ShowErrorMessage(STR_FACE_SAVE_DONE, INVALID_STRING_ID, WL_INFO);
				break;

			/* Toggle gender (male/female) button */
			case WID_SCMF_MALE:
			case WID_SCMF_FEMALE:
			case WID_SCMF_MALE2:
			case WID_SCMF_FEMALE2:
				SetCompanyManagerFaceBits(this->face, CMFV_GENDER, this->ge, (widget == WID_SCMF_FEMALE || widget == WID_SCMF_FEMALE2));
				ScaleAllCompanyManagerFaceBits(this->face);
				this->UpdateData();
				this->SetDirty();
				break;

			/* Randomize face button */
			case WID_SCMF_RANDOM_NEW_FACE:
				RandomCompanyManagerFaceBits(this->face, this->ge, this->advanced, _interactive_random);
				this->UpdateData();
				this->SetDirty();
				break;

			/* Toggle ethnicity (european/african) button */
			case WID_SCMF_ETHNICITY_EUR:
			case WID_SCMF_ETHNICITY_AFR:
				SetCompanyManagerFaceBits(this->face, CMFV_ETHNICITY, this->ge, widget - WID_SCMF_ETHNICITY_EUR);
				ScaleAllCompanyManagerFaceBits(this->face);
				this->UpdateData();
				this->SetDirty();
				break;

			default:
				/* Here all buttons from WID_SCMF_HAS_MOUSTACHE_EARRING to WID_SCMF_GLASSES_R are handled.
				 * First it checks which CompanyManagerFaceVariable is being changed, and then either
				 * a: invert the value for boolean variables, or
				 * b: it checks inside of IncreaseCompanyManagerFaceBits() if a left (_L) butten is pressed and then decrease else increase the variable */
				if (widget >= WID_SCMF_HAS_MOUSTACHE_EARRING && widget <= WID_SCMF_GLASSES_R) {
					CompanyManagerFaceVariable cmfv; // which CompanyManagerFaceVariable shall be edited

					if (widget < WID_SCMF_EYECOLOUR_L) { // Bool buttons
						switch (widget - WID_SCMF_HAS_MOUSTACHE_EARRING) {
							default: NOT_REACHED();
							case 0: cmfv = this->is_female ? CMFV_HAS_TIE_EARRING : CMFV_HAS_MOUSTACHE; break; // Has earring/moustache button
							case 1: cmfv = CMFV_HAS_GLASSES; break; // Has glasses button
						}
						SetCompanyManagerFaceBits(this->face, cmfv, this->ge, !GetCompanyManagerFaceBits(this->face, cmfv, this->ge));
						ScaleAllCompanyManagerFaceBits(this->face);
					} else { // Value buttons
						switch ((widget - WID_SCMF_EYECOLOUR_L) / 3) {
							default: NOT_REACHED();
							case 0: cmfv = CMFV_EYE_COLOUR; break;  // Eye colour buttons
							case 1: cmfv = CMFV_CHIN; break;        // Chin buttons
							case 2: cmfv = CMFV_EYEBROWS; break;    // Eyebrows buttons
							case 3: cmfv = this->is_moust_male ? CMFV_MOUSTACHE : CMFV_LIPS; break; // Moustache or lips buttons
							case 4: cmfv = CMFV_NOSE; break;        // Nose buttons
							case 5: cmfv = CMFV_HAIR; break;        // Hair buttons
							case 6: cmfv = CMFV_JACKET; break;      // Jacket buttons
							case 7: cmfv = CMFV_COLLAR; break;      // Collar buttons
							case 8: cmfv = CMFV_TIE_EARRING; break; // Tie/earring buttons
							case 9: cmfv = CMFV_GLASSES; break;     // Glasses buttons
						}
						/* 0 == left (_L), 1 == middle or 2 == right (_R) - button click */
						IncreaseCompanyManagerFaceBits(this->face, cmfv, this->ge, (((widget - WID_SCMF_EYECOLOUR_L) % 3) != 0) ? 1 : -1);
					}
					this->UpdateData();
					this->SetDirty();
				}
				break;
		}
	}

	void OnQueryTextFinished(char *str) override
	{
		if (str == nullptr) return;
		/* Set a new company manager face number */
		if (!StrEmpty(str)) {
			this->face = strtoul(str, nullptr, 10);
			ScaleAllCompanyManagerFaceBits(this->face);
			ShowErrorMessage(STR_FACE_FACECODE_SET, INVALID_STRING_ID, WL_INFO);
			this->UpdateData();
			this->SetDirty();
		} else {
			ShowErrorMessage(STR_FACE_FACECODE_ERR, INVALID_STRING_ID, WL_INFO);
		}
	}
};

/** Company manager face selection window description */
static WindowDesc _select_company_manager_face_desc(
	WDP_AUTO, "company_face", 0, 0,
	WC_COMPANY_MANAGER_FACE, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_select_company_manager_face_widgets, lengthof(_nested_select_company_manager_face_widgets)
);

/**
 * Open the simple/advanced company manager face selection window
 *
 * @param parent the parent company window
 */
static void DoSelectCompanyManagerFace(Window *parent)
{
	if (!Company::IsValidID((CompanyID)parent->window_number)) return;

	if (BringWindowToFrontById(WC_COMPANY_MANAGER_FACE, parent->window_number)) return;
	new SelectCompanyManagerFaceWindow(&_select_company_manager_face_desc, parent);
}

static const NWidgetPart _nested_company_infrastructure_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_CI_CAPTION), SetDataTip(STR_COMPANY_INFRASTRUCTURE_VIEW_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_VERTICAL), SetPadding(WidgetDimensions::unscaled.framerect), SetPIP(0, WidgetDimensions::unscaled.vsep_normal, 0),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_wide, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_RAIL_DESC), SetMinimalTextLines(2, 0), SetFill(1, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_RAIL_COUNT), SetMinimalTextLines(2, 0), SetFill(0, 1),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_wide, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_ROAD_DESC), SetMinimalTextLines(2, 0), SetFill(1, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_ROAD_COUNT), SetMinimalTextLines(2, 0), SetFill(0, 1),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_wide, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_TRAM_DESC), SetMinimalTextLines(2, 0), SetFill(1, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_TRAM_COUNT), SetMinimalTextLines(2, 0), SetFill(0, 1),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_wide, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_WATER_DESC), SetMinimalTextLines(2, 0), SetFill(1, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_WATER_COUNT), SetMinimalTextLines(2, 0), SetFill(0, 1),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_wide, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_STATION_DESC), SetMinimalTextLines(3, 0), SetFill(1, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_STATION_COUNT), SetMinimalTextLines(3, 0), SetFill(0, 1),
			EndContainer(),
			NWidget(NWID_HORIZONTAL), SetPIP(0, WidgetDimensions::unscaled.hsep_wide, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_TOTAL_DESC), SetFill(1, 0),
				NWidget(WWT_EMPTY, COLOUR_GREY, WID_CI_TOTAL), SetFill(0, 1),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

/**
 * Window with detailed information about the company's infrastructure.
 */
struct CompanyInfrastructureWindow : Window
{
	RailTypes railtypes; ///< Valid railtypes.
	RoadTypes roadtypes; ///< Valid roadtypes.

	uint total_width; ///< String width of the total cost line.

	CompanyInfrastructureWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc)
	{
		this->UpdateRailRoadTypes();

		this->InitNested(window_number);
		this->owner = (Owner)this->window_number;
	}

	void UpdateRailRoadTypes()
	{
		this->railtypes = RAILTYPES_NONE;
		this->roadtypes = ROADTYPES_NONE;

		/* Find the used railtypes. */
		for (const Engine *e : Engine::IterateType(VEH_TRAIN)) {
			if (!HasBit(e->info.climates, _settings_game.game_creation.landscape)) continue;

			this->railtypes |= GetRailTypeInfo(e->u.rail.railtype)->introduces_railtypes;
		}

		/* Get the date introduced railtypes as well. */
		this->railtypes = AddDateIntroducedRailTypes(this->railtypes, MAX_DAY);

		/* Find the used roadtypes. */
		for (const Engine *e : Engine::IterateType(VEH_ROAD)) {
			if (!HasBit(e->info.climates, _settings_game.game_creation.landscape)) continue;

			this->roadtypes |= GetRoadTypeInfo(e->u.road.roadtype)->introduces_roadtypes;
		}

		/* Get the date introduced roadtypes as well. */
		this->roadtypes = AddDateIntroducedRoadTypes(this->roadtypes, MAX_DAY);
		this->roadtypes &= ~_roadtypes_hidden_mask;
	}

	/** Get total infrastructure maintenance cost. */
	Money GetTotalMaintenanceCost() const
	{
		const Company *c = Company::Get((CompanyID)this->window_number);
		Money total;

		uint32 rail_total = c->infrastructure.GetRailTotal();
		for (RailType rt = RAILTYPE_BEGIN; rt != RAILTYPE_END; rt++) {
			if (HasBit(this->railtypes, rt)) total += RailMaintenanceCost(rt, c->infrastructure.rail[rt], rail_total);
		}
		total += SignalMaintenanceCost(c->infrastructure.signal);

		uint32 road_total = c->infrastructure.GetRoadTotal();
		uint32 tram_total = c->infrastructure.GetTramTotal();
		for (RoadType rt = ROADTYPE_BEGIN; rt != ROADTYPE_END; rt++) {
			if (HasBit(this->roadtypes, rt)) total += RoadMaintenanceCost(rt, c->infrastructure.road[rt], RoadTypeIsRoad(rt) ? road_total : tram_total);
		}

		total += CanalMaintenanceCost(c->infrastructure.water);
		total += StationMaintenanceCost(c->infrastructure.station);
		total += AirportMaintenanceCost(c->index);

		return total;
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_CI_CAPTION:
				SetDParam(0, (CompanyID)this->window_number);
				break;
		}
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		const Company *c = Company::Get((CompanyID)this->window_number);

		switch (widget) {
			case WID_CI_RAIL_DESC: {
				uint lines = 1; // Starts at 1 because a line is also required for the section title

				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_RAIL_SECT).width);

				for (const auto &rt : _sorted_railtypes) {
					if (HasBit(this->railtypes, rt)) {
						lines++;
						SetDParam(0, GetRailTypeInfo(rt)->strings.name);
						size->width = std::max(size->width, GetStringBoundingBox(STR_WHITE_STRING).width + WidgetDimensions::scaled.hsep_indent);
					}
				}
				if (this->railtypes != RAILTYPES_NONE) {
					lines++;
					size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_SIGNALS).width + WidgetDimensions::scaled.hsep_indent);
				}

				size->height = std::max(size->height, lines * FONT_HEIGHT_NORMAL);
				break;
			}

			case WID_CI_ROAD_DESC:
			case WID_CI_TRAM_DESC: {
				uint lines = 1; // Starts at 1 because a line is also required for the section title

				size->width = std::max(size->width, GetStringBoundingBox(widget == WID_CI_ROAD_DESC ? STR_COMPANY_INFRASTRUCTURE_VIEW_ROAD_SECT : STR_COMPANY_INFRASTRUCTURE_VIEW_TRAM_SECT).width);

				for (const auto &rt : _sorted_roadtypes) {
					if (HasBit(this->roadtypes, rt) && RoadTypeIsRoad(rt) == (widget == WID_CI_ROAD_DESC)) {
						lines++;
						SetDParam(0, GetRoadTypeInfo(rt)->strings.name);
						size->width = std::max(size->width, GetStringBoundingBox(STR_WHITE_STRING).width + WidgetDimensions::scaled.hsep_indent);
					}
				}

				size->height = std::max(size->height, lines * FONT_HEIGHT_NORMAL);
				break;
			}

			case WID_CI_WATER_DESC:
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_WATER_SECT).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_CANALS).width + WidgetDimensions::scaled.hsep_indent);
				break;

			case WID_CI_STATION_DESC:
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_STATION_SECT).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_STATIONS).width + WidgetDimensions::scaled.hsep_indent);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_AIRPORTS).width + WidgetDimensions::scaled.hsep_indent);
				break;

			case WID_CI_RAIL_COUNT:
			case WID_CI_ROAD_COUNT:
			case WID_CI_TRAM_COUNT:
			case WID_CI_WATER_COUNT:
			case WID_CI_STATION_COUNT:
			case WID_CI_TOTAL: {
				/* Find the maximum count that is displayed. */
				uint32 max_val = 1000;  // Some random number to reserve enough space.
				Money max_cost = 10000; // Some random number to reserve enough space.
				uint32 rail_total = c->infrastructure.GetRailTotal();
				for (RailType rt = RAILTYPE_BEGIN; rt < RAILTYPE_END; rt++) {
					max_val = std::max(max_val, c->infrastructure.rail[rt]);
					max_cost = std::max(max_cost, RailMaintenanceCost(rt, c->infrastructure.rail[rt], rail_total));
				}
				max_val = std::max(max_val, c->infrastructure.signal);
				max_cost = std::max(max_cost, SignalMaintenanceCost(c->infrastructure.signal));
				uint32 road_total = c->infrastructure.GetRoadTotal();
				uint32 tram_total = c->infrastructure.GetTramTotal();
				for (RoadType rt = ROADTYPE_BEGIN; rt < ROADTYPE_END; rt++) {
					max_val = std::max(max_val, c->infrastructure.road[rt]);
					max_cost = std::max(max_cost, RoadMaintenanceCost(rt, c->infrastructure.road[rt], RoadTypeIsRoad(rt) ? road_total : tram_total));

				}
				max_val = std::max(max_val, c->infrastructure.water);
				max_cost = std::max(max_cost, CanalMaintenanceCost(c->infrastructure.water));
				max_val = std::max(max_val, c->infrastructure.station);
				max_cost = std::max(max_cost, StationMaintenanceCost(c->infrastructure.station));
				max_val = std::max(max_val, c->infrastructure.airport);
				max_cost = std::max(max_cost, AirportMaintenanceCost(c->index));

				SetDParamMaxValue(0, max_val);
				uint count_width = GetStringBoundingBox(STR_WHITE_COMMA).width + WidgetDimensions::scaled.hsep_indent; // Reserve some wiggle room

				if (_settings_game.economy.infrastructure_maintenance) {
					SetDParamMaxValue(0, this->GetTotalMaintenanceCost() * 12); // Convert to per year
					this->total_width = GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_TOTAL).width + WidgetDimensions::scaled.hsep_indent * 2;
					size->width = std::max(size->width, this->total_width);

					SetDParamMaxValue(0, max_cost * 12); // Convert to per year
					count_width += std::max(this->total_width, GetStringBoundingBox(STR_COMPANY_INFRASTRUCTURE_VIEW_TOTAL).width);
				}

				size->width = std::max(size->width, count_width);

				/* Set height of the total line. */
				if (widget == WID_CI_TOTAL) {
					size->height = _settings_game.economy.infrastructure_maintenance ? std::max<uint>(size->height, WidgetDimensions::scaled.vsep_normal + FONT_HEIGHT_NORMAL) : 0;
				}
				break;
			}
		}
	}

	/**
	 * Helper for drawing the counts line.
	 * @param r            The bounds to draw in.
	 * @param y            The y position to draw at.
	 * @param count        The count to show on this line.
	 * @param monthly_cost The monthly costs.
	 */
	void DrawCountLine(const Rect &r, int &y, int count, Money monthly_cost) const
	{
		SetDParam(0, count);
		DrawString(r.left, r.right, y += FONT_HEIGHT_NORMAL, STR_WHITE_COMMA, TC_FROMSTRING, SA_RIGHT);

		if (_settings_game.economy.infrastructure_maintenance) {
			SetDParam(0, monthly_cost * 12); // Convert to per year
			Rect tr = r.WithWidth(this->total_width, _current_text_dir == TD_RTL);
			DrawString(tr.left, tr.right, y, STR_COMPANY_INFRASTRUCTURE_VIEW_TOTAL, TC_FROMSTRING, SA_RIGHT);
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		const Company *c = Company::Get((CompanyID)this->window_number);

		int y = r.top;

		Rect ir = r.Indent(WidgetDimensions::scaled.hsep_indent, _current_text_dir == TD_RTL);
		switch (widget) {
			case WID_CI_RAIL_DESC:
				DrawString(r.left, r.right, y, STR_COMPANY_INFRASTRUCTURE_VIEW_RAIL_SECT);

				if (this->railtypes != RAILTYPES_NONE) {
					/* Draw name of each valid railtype. */
					for (const auto &rt : _sorted_railtypes) {
						if (HasBit(this->railtypes, rt)) {
							SetDParam(0, GetRailTypeInfo(rt)->strings.name);
							DrawString(ir.left, ir.right, y += FONT_HEIGHT_NORMAL, STR_WHITE_STRING);
						}
					}
					DrawString(ir.left, ir.right, y += FONT_HEIGHT_NORMAL, STR_COMPANY_INFRASTRUCTURE_VIEW_SIGNALS);
				} else {
					/* No valid railtype. */
					DrawString(ir.left, ir.right, y += FONT_HEIGHT_NORMAL, STR_COMPANY_VIEW_INFRASTRUCTURE_NONE);
				}

				break;

			case WID_CI_RAIL_COUNT: {
				/* Draw infrastructure count for each valid railtype. */
				uint32 rail_total = c->infrastructure.GetRailTotal();
				for (const auto &rt : _sorted_railtypes) {
					if (HasBit(this->railtypes, rt)) {
						this->DrawCountLine(r, y, c->infrastructure.rail[rt], RailMaintenanceCost(rt, c->infrastructure.rail[rt], rail_total));
					}
				}
				if (this->railtypes != RAILTYPES_NONE) {
					this->DrawCountLine(r, y, c->infrastructure.signal, SignalMaintenanceCost(c->infrastructure.signal));
				}
				break;
			}

			case WID_CI_ROAD_DESC:
			case WID_CI_TRAM_DESC: {
				DrawString(r.left, r.right, y, widget == WID_CI_ROAD_DESC ? STR_COMPANY_INFRASTRUCTURE_VIEW_ROAD_SECT : STR_COMPANY_INFRASTRUCTURE_VIEW_TRAM_SECT);

				/* Draw name of each valid roadtype. */
				for (const auto &rt : _sorted_roadtypes) {
					if (HasBit(this->roadtypes, rt) && RoadTypeIsRoad(rt) == (widget == WID_CI_ROAD_DESC)) {
						SetDParam(0, GetRoadTypeInfo(rt)->strings.name);
						DrawString(ir.left, ir.right, y += FONT_HEIGHT_NORMAL, STR_WHITE_STRING);
					}
				}

				break;
			}

			case WID_CI_ROAD_COUNT:
			case WID_CI_TRAM_COUNT: {
				uint32 road_tram_total = widget == WID_CI_ROAD_COUNT ? c->infrastructure.GetRoadTotal() : c->infrastructure.GetTramTotal();
				for (const auto &rt : _sorted_roadtypes) {
					if (HasBit(this->roadtypes, rt) && RoadTypeIsRoad(rt) == (widget == WID_CI_ROAD_COUNT)) {
						this->DrawCountLine(r, y, c->infrastructure.road[rt], RoadMaintenanceCost(rt, c->infrastructure.road[rt], road_tram_total));
					}
				}
				break;
			}

			case WID_CI_WATER_DESC:
				DrawString(r.left, r.right, y, STR_COMPANY_INFRASTRUCTURE_VIEW_WATER_SECT);
				DrawString(ir.left, ir.right, y += FONT_HEIGHT_NORMAL, STR_COMPANY_INFRASTRUCTURE_VIEW_CANALS);
				break;

			case WID_CI_WATER_COUNT:
				this->DrawCountLine(r, y, c->infrastructure.water, CanalMaintenanceCost(c->infrastructure.water));
				break;

			case WID_CI_TOTAL:
				if (_settings_game.economy.infrastructure_maintenance) {
					Rect tr = r.WithWidth(this->total_width, _current_text_dir == TD_RTL);
					GfxFillRect(tr.left, y, tr.right, y, PC_WHITE);
					y += WidgetDimensions::scaled.vsep_normal;
					SetDParam(0, this->GetTotalMaintenanceCost() * 12); // Convert to per year
					DrawString(tr.left, tr.right, y, STR_COMPANY_INFRASTRUCTURE_VIEW_TOTAL, TC_FROMSTRING, SA_RIGHT);
				}
				break;

			case WID_CI_STATION_DESC:
				DrawString(r.left, r.right, y, STR_COMPANY_INFRASTRUCTURE_VIEW_STATION_SECT);
				DrawString(ir.left, ir.right, y += FONT_HEIGHT_NORMAL, STR_COMPANY_INFRASTRUCTURE_VIEW_STATIONS);
				DrawString(ir.left, ir.right, y += FONT_HEIGHT_NORMAL, STR_COMPANY_INFRASTRUCTURE_VIEW_AIRPORTS);
				break;

			case WID_CI_STATION_COUNT:
				this->DrawCountLine(r, y, c->infrastructure.station, StationMaintenanceCost(c->infrastructure.station));
				this->DrawCountLine(r, y, c->infrastructure.airport, AirportMaintenanceCost(c->index));
				break;
		}
	}

	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (!gui_scope) return;

		this->UpdateRailRoadTypes();
		this->ReInit();
	}
};

static WindowDesc _company_infrastructure_desc(
	WDP_AUTO, "company_infrastructure", 0, 0,
	WC_COMPANY_INFRASTRUCTURE, WC_NONE,
	0,
	_nested_company_infrastructure_widgets, lengthof(_nested_company_infrastructure_widgets)
);

/**
 * Open the infrastructure window of a company.
 * @param company Company to show infrastructure of.
 */
static void ShowCompanyInfrastructure(CompanyID company)
{
	if (!Company::IsValidID(company)) return;
	AllocateWindowDescFront<CompanyInfrastructureWindow>(&_company_infrastructure_desc, company);
}

static const NWidgetPart _nested_company_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_GREY),
		NWidget(WWT_CAPTION, COLOUR_GREY, WID_C_CAPTION), SetDataTip(STR_COMPANY_VIEW_CAPTION, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
		NWidget(WWT_SHADEBOX, COLOUR_GREY),
		NWidget(WWT_STICKYBOX, COLOUR_GREY),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_GREY),
		NWidget(NWID_HORIZONTAL), SetPIP(4, 6, 4),
			NWidget(NWID_VERTICAL), SetPIP(4, 2, 4),
				NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_FACE), SetMinimalSize(92, 119), SetFill(1, 0),
				NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_FACE_TITLE), SetFill(1, 1), SetMinimalTextLines(2, 0),
			EndContainer(),
			NWidget(NWID_VERTICAL),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_VERTICAL), SetPIP(4, 5, 5),
						NWidget(WWT_TEXT, COLOUR_GREY, WID_C_DESC_INAUGURATION), SetDataTip(STR_COMPANY_VIEW_INAUGURATED_TITLE, STR_NULL), SetFill(1, 0),
						NWidget(NWID_HORIZONTAL), SetPIP(0, 5, 0),
							NWidget(WWT_LABEL, COLOUR_GREY, WID_C_DESC_COLOUR_SCHEME), SetDataTip(STR_COMPANY_VIEW_COLOUR_SCHEME_TITLE, STR_NULL),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_DESC_COLOUR_SCHEME_EXAMPLE), SetMinimalSize(30, 0), SetFill(0, 1),
							NWidget(NWID_SPACER), SetFill(1, 0),
						EndContainer(),
						NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
							NWidget(NWID_VERTICAL),
								NWidget(WWT_TEXT, COLOUR_GREY, WID_C_DESC_VEHICLE), SetDataTip(STR_COMPANY_VIEW_VEHICLES_TITLE, STR_NULL),
								NWidget(NWID_SPACER), SetFill(0, 1),
							EndContainer(),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_DESC_VEHICLE_COUNTS), SetMinimalTextLines(4, 0),
							NWidget(NWID_SPACER), SetFill(1, 0),
						EndContainer(),
					EndContainer(),
					NWidget(NWID_VERTICAL), SetPIP(4, 2, 4),
						NWidget(NWID_SELECTION, INVALID_COLOUR, WID_C_SELECT_VIEW_BUILD_HQ),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_VIEW_HQ), SetDataTip(STR_COMPANY_VIEW_VIEW_HQ_BUTTON, STR_COMPANY_VIEW_VIEW_HQ_TOOLTIP),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_C_BUILD_HQ), SetDataTip(STR_COMPANY_VIEW_BUILD_HQ_BUTTON, STR_COMPANY_VIEW_BUILD_HQ_TOOLTIP),
						EndContainer(),
						NWidget(NWID_SELECTION, INVALID_COLOUR, WID_C_SELECT_RELOCATE),
							NWidget(WWT_TEXTBTN, COLOUR_GREY, WID_C_RELOCATE_HQ), SetDataTip(STR_COMPANY_VIEW_RELOCATE_HQ, STR_COMPANY_VIEW_RELOCATE_COMPANY_HEADQUARTERS),
							NWidget(NWID_SPACER),
						EndContainer(),
						NWidget(NWID_SPACER), SetFill(0, 1),
					EndContainer(),
				EndContainer(),
				NWidget(WWT_TEXT, COLOUR_GREY, WID_C_DESC_COMPANY_VALUE), SetDataTip(STR_COMPANY_VIEW_COMPANY_VALUE, STR_NULL), SetFill(1, 0),
				NWidget(NWID_VERTICAL), SetPIP(4, 2, 4),
					NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
						NWidget(NWID_VERTICAL),
							NWidget(WWT_TEXT, COLOUR_GREY, WID_C_DESC_INFRASTRUCTURE), SetDataTip(STR_COMPANY_VIEW_INFRASTRUCTURE, STR_NULL),
							NWidget(NWID_SPACER), SetFill(0, 1),
						EndContainer(),
						NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_DESC_INFRASTRUCTURE_COUNTS), SetMinimalTextLines(5, 0), SetFill(1, 0),
						NWidget(NWID_VERTICAL),
							NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_VIEW_INFRASTRUCTURE), SetDataTip(STR_COMPANY_VIEW_INFRASTRUCTURE_BUTTON, STR_COMPANY_VIEW_INFRASTRUCTURE_TOOLTIP),
							NWidget(NWID_SPACER),
						EndContainer(),
					EndContainer(),
				EndContainer(),
				NWidget(NWID_HORIZONTAL),
					NWidget(NWID_SELECTION, INVALID_COLOUR, WID_C_SELECT_DESC_OWNERS),
						NWidget(NWID_VERTICAL), SetPIP(5, 5, 4),
							NWidget(WWT_EMPTY, INVALID_COLOUR, WID_C_DESC_OWNERS), SetMinimalTextLines(MAX_COMPANY_SHARE_OWNERS, 0),
							NWidget(NWID_SPACER), SetFill(0, 1),
						EndContainer(),
					EndContainer(),
					/* Multi player buttons. */
					NWidget(NWID_VERTICAL), SetPIP(4, 2, 4),
						NWidget(NWID_SPACER), SetFill(0, 1),
						NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
							NWidget(NWID_SPACER), SetFill(1, 0),
							NWidget(NWID_SELECTION, INVALID_COLOUR, WID_C_SELECT_GIVE_MONEY),
								NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_GIVE_MONEY), SetDataTip(STR_COMPANY_VIEW_GIVE_MONEY_BUTTON, STR_COMPANY_VIEW_GIVE_MONEY_TOOLTIP),
							EndContainer(),
						EndContainer(),
						NWidget(NWID_HORIZONTAL), SetPIP(0, 4, 0),
							NWidget(WWT_EMPTY, COLOUR_GREY, WID_C_HAS_PASSWORD),
							NWidget(NWID_SELECTION, INVALID_COLOUR, WID_C_SELECT_MULTIPLAYER),
								NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_COMPANY_PASSWORD), SetDataTip(STR_COMPANY_VIEW_PASSWORD, STR_COMPANY_VIEW_PASSWORD_TOOLTIP),
								NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_COMPANY_JOIN), SetDataTip(STR_COMPANY_VIEW_JOIN, STR_COMPANY_VIEW_JOIN_TOOLTIP),
							EndContainer(),
						EndContainer(),
					EndContainer(),
				EndContainer(),
			EndContainer(),
		EndContainer(),
	EndContainer(),
	/* Button bars at the bottom. */
	NWidget(NWID_SELECTION, INVALID_COLOUR, WID_C_SELECT_BUTTONS),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_NEW_FACE), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_NEW_FACE_BUTTON, STR_COMPANY_VIEW_NEW_FACE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_COLOUR_SCHEME), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_COLOUR_SCHEME_BUTTON, STR_COMPANY_VIEW_COLOUR_SCHEME_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_PRESIDENT_NAME), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_PRESIDENT_NAME_BUTTON, STR_COMPANY_VIEW_PRESIDENT_NAME_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_COMPANY_NAME), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_COMPANY_NAME_BUTTON, STR_COMPANY_VIEW_COMPANY_NAME_TOOLTIP),
		EndContainer(),
		NWidget(NWID_HORIZONTAL, NC_EQUALSIZE),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_BUY_SHARE), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_BUY_SHARE_BUTTON, STR_COMPANY_VIEW_BUY_SHARE_TOOLTIP),
			NWidget(WWT_PUSHTXTBTN, COLOUR_GREY, WID_C_SELL_SHARE), SetFill(1, 0), SetDataTip(STR_COMPANY_VIEW_SELL_SHARE_BUTTON, STR_COMPANY_VIEW_SELL_SHARE_TOOLTIP),
		EndContainer(),
	EndContainer(),
};

int GetAmountOwnedBy(const Company *c, Owner owner)
{
	auto share_owned_by = [owner](auto share_owner) { return share_owner == owner; };
	return std::count_if(c->share_owners.begin(), c->share_owners.end(), share_owned_by);
}

/** Strings for the company vehicle counts */
static const StringID _company_view_vehicle_count_strings[] = {
	STR_COMPANY_VIEW_TRAINS, STR_COMPANY_VIEW_ROAD_VEHICLES, STR_COMPANY_VIEW_SHIPS, STR_COMPANY_VIEW_AIRCRAFT
};

/**
 * Window with general information about a company
 */
struct CompanyWindow : Window
{
	CompanyWidgets query_widget;

	/** Display planes in the company window. */
	enum CompanyWindowPlanes {
		/* Display planes of the #WID_C_SELECT_MULTIPLAYER selection widget. */
		CWP_MP_C_PWD = 0, ///< Display the company password button.
		CWP_MP_C_JOIN,    ///< Display the join company button.

		/* Display planes of the #WID_C_SELECT_VIEW_BUILD_HQ selection widget. */
		CWP_VB_VIEW = 0,  ///< Display the view button
		CWP_VB_BUILD,     ///< Display the build button

		/* Display planes of the #WID_C_SELECT_RELOCATE selection widget. */
		CWP_RELOCATE_SHOW = 0, ///< Show the relocate HQ button.
		CWP_RELOCATE_HIDE,     ///< Hide the relocate HQ button.

		/* Display planes of the #WID_C_SELECT_BUTTONS selection widget. */
		CWP_BUTTONS_LOCAL = 0, ///< Buttons of the local company.
		CWP_BUTTONS_OTHER,     ///< Buttons of the other companies.
	};

	CompanyWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
		this->owner = (Owner)this->window_number;
		this->OnInvalidateData();
	}

	void OnPaint() override
	{
		const Company *c = Company::Get((CompanyID)this->window_number);
		bool local = this->window_number == _local_company;

		if (!this->IsShaded()) {
			bool reinit = false;

			/* Button bar selection. */
			int plane = local ? CWP_BUTTONS_LOCAL : CWP_BUTTONS_OTHER;
			NWidgetStacked *wi = this->GetWidget<NWidgetStacked>(WID_C_SELECT_BUTTONS);
			if (plane != wi->shown_plane) {
				wi->SetDisplayedPlane(plane);
				this->InvalidateData();
				reinit = true;
			}

			/* Build HQ button handling. */
			plane = (local && c->location_of_HQ == INVALID_TILE) ? CWP_VB_BUILD : CWP_VB_VIEW;
			wi = this->GetWidget<NWidgetStacked>(WID_C_SELECT_VIEW_BUILD_HQ);
			if (plane != wi->shown_plane) {
				wi->SetDisplayedPlane(plane);
				reinit = true;
			}

			this->SetWidgetDisabledState(WID_C_VIEW_HQ, c->location_of_HQ == INVALID_TILE);

			/* Enable/disable 'Relocate HQ' button. */
			plane = (!local || c->location_of_HQ == INVALID_TILE) ? CWP_RELOCATE_HIDE : CWP_RELOCATE_SHOW;
			wi = this->GetWidget<NWidgetStacked>(WID_C_SELECT_RELOCATE);
			if (plane != wi->shown_plane) {
				wi->SetDisplayedPlane(plane);
				reinit = true;
			}

			/* Owners of company */
			auto invalid_owner = [](auto owner) { return owner == INVALID_COMPANY; };
			plane = std::all_of(c->share_owners.begin(), c->share_owners.end(), invalid_owner) ? SZSP_HORIZONTAL : 0;
			wi = this->GetWidget<NWidgetStacked>(WID_C_SELECT_DESC_OWNERS);
			if (plane != wi->shown_plane) {
				wi->SetDisplayedPlane(plane);
				reinit = true;
			}

			/* Enable/disable 'Give money' button. */
			plane = ((local || _local_company == COMPANY_SPECTATOR || !_settings_game.economy.give_money) ? SZSP_NONE : 0);
			wi = this->GetWidget<NWidgetStacked>(WID_C_SELECT_GIVE_MONEY);
			if (plane != wi->shown_plane) {
				wi->SetDisplayedPlane(plane);
				reinit = true;
			}

			/* Multiplayer buttons. */
			plane = ((!_networking) ? (int)SZSP_NONE : (int)(local ? CWP_MP_C_PWD : CWP_MP_C_JOIN));
			wi = this->GetWidget<NWidgetStacked>(WID_C_SELECT_MULTIPLAYER);
			if (plane != wi->shown_plane) {
				wi->SetDisplayedPlane(plane);
				reinit = true;
			}
			this->SetWidgetDisabledState(WID_C_COMPANY_JOIN,   c->is_ai);

			if (reinit) {
				this->ReInit();
				return;
			}
		}

		this->DrawWidgets();
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_C_FACE: {
				Dimension face_size = GetSpriteSize(SPR_GRADIENT);
				size->width  = std::max(size->width,  face_size.width);
				size->height = std::max(size->height, face_size.height);
				break;
			}

			case WID_C_DESC_COLOUR_SCHEME_EXAMPLE: {
				Point offset;
				Dimension d = GetSpriteSize(SPR_VEH_BUS_SW_VIEW, &offset);
				d.width -= offset.x;
				d.height -= offset.y;
				*size = maxdim(*size, d);
				break;
			}

			case WID_C_DESC_COMPANY_VALUE:
				SetDParam(0, INT64_MAX); // Arguably the maximum company value
				size->width = GetStringBoundingBox(STR_COMPANY_VIEW_COMPANY_VALUE).width;
				break;

			case WID_C_DESC_VEHICLE_COUNTS:
				SetDParamMaxValue(0, 5000); // Maximum number of vehicles
				for (uint i = 0; i < lengthof(_company_view_vehicle_count_strings); i++) {
					size->width = std::max(size->width, GetStringBoundingBox(_company_view_vehicle_count_strings[i]).width);
				}
				break;

			case WID_C_DESC_INFRASTRUCTURE_COUNTS:
				SetDParamMaxValue(0, UINT_MAX);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_RAIL).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_ROAD).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_WATER).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_STATION).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_AIRPORT).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_NONE).width);
				break;

			case WID_C_DESC_OWNERS: {
				for (const Company *c2 : Company::Iterate()) {
					SetDParamMaxValue(0, 75);
					SetDParam(1, c2->index);

					size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_SHARES_OWNED_BY).width);
				}
				break;
			}

			case WID_C_VIEW_HQ:
			case WID_C_BUILD_HQ:
			case WID_C_RELOCATE_HQ:
			case WID_C_VIEW_INFRASTRUCTURE:
			case WID_C_GIVE_MONEY:
			case WID_C_COMPANY_PASSWORD:
			case WID_C_COMPANY_JOIN:
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_VIEW_HQ_BUTTON).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_BUILD_HQ_BUTTON).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_RELOCATE_HQ).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_INFRASTRUCTURE_BUTTON).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_GIVE_MONEY_BUTTON).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_PASSWORD).width);
				size->width = std::max(size->width, GetStringBoundingBox(STR_COMPANY_VIEW_JOIN).width);
				break;

			case WID_C_HAS_PASSWORD:
				*size = maxdim(*size, GetSpriteSize(SPR_LOCK));
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		const Company *c = Company::Get((CompanyID)this->window_number);
		switch (widget) {
			case WID_C_FACE:
				DrawCompanyManagerFace(c->face, c->colour, r.left, r.top);
				break;

			case WID_C_FACE_TITLE:
				SetDParam(0, c->index);
				DrawStringMultiLine(r.left, r.right, r.top, r.bottom, STR_COMPANY_VIEW_PRESIDENT_MANAGER_TITLE, TC_FROMSTRING, SA_HOR_CENTER);
				break;

			case WID_C_DESC_COLOUR_SCHEME_EXAMPLE: {
				Point offset;
				Dimension d = GetSpriteSize(SPR_VEH_BUS_SW_VIEW, &offset);
				d.height -= offset.y;
				DrawSprite(SPR_VEH_BUS_SW_VIEW, COMPANY_SPRITE_COLOUR(c->index), r.left - offset.x, CenterBounds(r.top, r.bottom, d.height) - offset.y);
				break;
			}

			case WID_C_DESC_VEHICLE_COUNTS: {
				uint amounts[4];
				amounts[0] = c->group_all[VEH_TRAIN].num_vehicle;
				amounts[1] = c->group_all[VEH_ROAD].num_vehicle;
				amounts[2] = c->group_all[VEH_SHIP].num_vehicle;
				amounts[3] = c->group_all[VEH_AIRCRAFT].num_vehicle;

				int y = r.top;
				if (amounts[0] + amounts[1] + amounts[2] + amounts[3] == 0) {
					DrawString(r.left, r.right, y, STR_COMPANY_VIEW_VEHICLES_NONE);
				} else {
					static_assert(lengthof(amounts) == lengthof(_company_view_vehicle_count_strings));

					for (uint i = 0; i < lengthof(amounts); i++) {
						if (amounts[i] != 0) {
							SetDParam(0, amounts[i]);
							DrawString(r.left, r.right, y, _company_view_vehicle_count_strings[i]);
							y += FONT_HEIGHT_NORMAL;
						}
					}
				}
				break;
			}

			case WID_C_DESC_INFRASTRUCTURE_COUNTS: {
				uint y = r.top;

				/* Collect rail and road counts. */
				uint rail_pieces = c->infrastructure.signal;
				uint road_pieces = 0;
				for (uint i = 0; i < lengthof(c->infrastructure.rail); i++) rail_pieces += c->infrastructure.rail[i];
				for (uint i = 0; i < lengthof(c->infrastructure.road); i++) road_pieces += c->infrastructure.road[i];

				if (rail_pieces == 0 && road_pieces == 0 && c->infrastructure.water == 0 && c->infrastructure.station == 0 && c->infrastructure.airport == 0) {
					DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_NONE);
				} else {
					if (rail_pieces != 0) {
						SetDParam(0, rail_pieces);
						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_RAIL);
						y += FONT_HEIGHT_NORMAL;
					}
					if (road_pieces != 0) {
						SetDParam(0, road_pieces);
						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_ROAD);
						y += FONT_HEIGHT_NORMAL;
					}
					if (c->infrastructure.water != 0) {
						SetDParam(0, c->infrastructure.water);
						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_WATER);
						y += FONT_HEIGHT_NORMAL;
					}
					if (c->infrastructure.station != 0) {
						SetDParam(0, c->infrastructure.station);
						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_STATION);
						y += FONT_HEIGHT_NORMAL;
					}
					if (c->infrastructure.airport != 0) {
						SetDParam(0, c->infrastructure.airport);
						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_INFRASTRUCTURE_AIRPORT);
					}
				}

				break;
			}

			case WID_C_DESC_OWNERS: {
				uint y = r.top;

				for (const Company *c2 : Company::Iterate()) {
					uint amt = GetAmountOwnedBy(c, c2->index);
					if (amt != 0) {
						SetDParam(0, amt * 25);
						SetDParam(1, c2->index);

						DrawString(r.left, r.right, y, STR_COMPANY_VIEW_SHARES_OWNED_BY);
						y += FONT_HEIGHT_NORMAL;
					}
				}
				break;
			}

			case WID_C_HAS_PASSWORD:
				if (_networking && NetworkCompanyIsPassworded(c->index)) {
					DrawSprite(SPR_LOCK, PAL_NONE, r.left, r.top);
				}
				break;
		}
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_C_CAPTION:
				SetDParam(0, (CompanyID)this->window_number);
				SetDParam(1, (CompanyID)this->window_number);
				break;

			case WID_C_DESC_INAUGURATION:
				SetDParam(0, Company::Get((CompanyID)this->window_number)->inaugurated_year);
				break;

			case WID_C_DESC_COMPANY_VALUE:
				SetDParam(0, CalculateCompanyValue(Company::Get((CompanyID)this->window_number)));
				break;
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_C_NEW_FACE: DoSelectCompanyManagerFace(this); break;

			case WID_C_COLOUR_SCHEME:
				ShowCompanyLiveryWindow((CompanyID)this->window_number, INVALID_GROUP);
				break;

			case WID_C_PRESIDENT_NAME:
				this->query_widget = WID_C_PRESIDENT_NAME;
				SetDParam(0, this->window_number);
				ShowQueryString(STR_PRESIDENT_NAME, STR_COMPANY_VIEW_PRESIDENT_S_NAME_QUERY_CAPTION, MAX_LENGTH_PRESIDENT_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				break;

			case WID_C_COMPANY_NAME:
				this->query_widget = WID_C_COMPANY_NAME;
				SetDParam(0, this->window_number);
				ShowQueryString(STR_COMPANY_NAME, STR_COMPANY_VIEW_COMPANY_NAME_QUERY_CAPTION, MAX_LENGTH_COMPANY_NAME_CHARS, this, CS_ALPHANUMERAL, QSF_ENABLE_DEFAULT | QSF_LEN_IN_CHARS);
				break;

			case WID_C_VIEW_HQ: {
				TileIndex tile = Company::Get((CompanyID)this->window_number)->location_of_HQ;
				if (_ctrl_pressed) {
					ShowExtraViewportWindow(tile);
				} else {
					ScrollMainWindowToTile(tile);
				}
				break;
			}

			case WID_C_BUILD_HQ:
				if ((byte)this->window_number != _local_company) return;
				if (this->IsWidgetLowered(WID_C_BUILD_HQ)) {
					ResetObjectToPlace();
					this->RaiseButtons();
					break;
				}
				SetObjectToPlaceWnd(SPR_CURSOR_HQ, PAL_NONE, HT_RECT, this);
				SetTileSelectSize(2, 2);
				this->LowerWidget(WID_C_BUILD_HQ);
				this->SetWidgetDirty(WID_C_BUILD_HQ);
				break;

			case WID_C_RELOCATE_HQ:
				if (this->IsWidgetLowered(WID_C_RELOCATE_HQ)) {
					ResetObjectToPlace();
					this->RaiseButtons();
					break;
				}
				SetObjectToPlaceWnd(SPR_CURSOR_HQ, PAL_NONE, HT_RECT, this);
				SetTileSelectSize(2, 2);
				this->LowerWidget(WID_C_RELOCATE_HQ);
				this->SetWidgetDirty(WID_C_RELOCATE_HQ);
				break;

			case WID_C_VIEW_INFRASTRUCTURE:
				ShowCompanyInfrastructure((CompanyID)this->window_number);
				break;

			case WID_C_GIVE_MONEY:
				this->query_widget = WID_C_GIVE_MONEY;
				ShowQueryString(STR_EMPTY, STR_COMPANY_VIEW_GIVE_MONEY_QUERY_CAPTION, 30, this, CS_NUMERAL, QSF_NONE);
				break;

			case WID_C_BUY_SHARE:
				Command<CMD_BUY_SHARE_IN_COMPANY>::Post(STR_ERROR_CAN_T_BUY_25_SHARE_IN_THIS, (CompanyID)this->window_number);
				break;

			case WID_C_SELL_SHARE:
				Command<CMD_SELL_SHARE_IN_COMPANY>::Post(STR_ERROR_CAN_T_SELL_25_SHARE_IN, (CompanyID)this->window_number);
				break;

			case WID_C_COMPANY_PASSWORD:
				if (this->window_number == _local_company) ShowNetworkCompanyPasswordWindow(this);
				break;

			case WID_C_COMPANY_JOIN: {
				this->query_widget = WID_C_COMPANY_JOIN;
				CompanyID company = (CompanyID)this->window_number;
				if (_network_server) {
					NetworkServerDoMove(CLIENT_ID_SERVER, company);
					MarkWholeScreenDirty();
				} else if (NetworkCompanyIsPassworded(company)) {
					/* ask for the password */
					ShowQueryString(STR_EMPTY, STR_NETWORK_NEED_COMPANY_PASSWORD_CAPTION, NETWORK_PASSWORD_LENGTH, this, CS_ALPHANUMERAL, QSF_PASSWORD);
				} else {
					/* just send the join command */
					NetworkClientRequestMove(company);
				}
				break;
			}
		}
	}

	void OnHundredthTick() override
	{
		/* redraw the window every now and then */
		this->SetDirty();
	}

	void OnPlaceObject(Point pt, TileIndex tile) override
	{
		if (Command<CMD_BUILD_OBJECT>::Post(STR_ERROR_CAN_T_BUILD_COMPANY_HEADQUARTERS, tile, OBJECT_HQ, 0) && !_shift_pressed) {
			ResetObjectToPlace();
			this->RaiseButtons();
		}
	}

	void OnPlaceObjectAbort() override
	{
		this->RaiseButtons();
	}

	void OnQueryTextFinished(char *str) override
	{
		if (str == nullptr) return;

		switch (this->query_widget) {
			default: NOT_REACHED();

			case WID_C_GIVE_MONEY: {
				Money money = (Money)(strtoull(str, nullptr, 10) / _currency->rate);
				uint32 money_c = Clamp(ClampToI32(money), 0, 20000000); // Clamp between 20 million and 0

				Command<CMD_GIVE_MONEY>::Post(STR_ERROR_CAN_T_GIVE_MONEY, money_c, (CompanyID)this->window_number);
				break;
			}

			case WID_C_PRESIDENT_NAME:
				Command<CMD_RENAME_PRESIDENT>::Post(STR_ERROR_CAN_T_CHANGE_PRESIDENT, str);
				break;

			case WID_C_COMPANY_NAME:
				Command<CMD_RENAME_COMPANY>::Post(STR_ERROR_CAN_T_CHANGE_COMPANY_NAME, str);
				break;

			case WID_C_COMPANY_JOIN:
				NetworkClientRequestMove((CompanyID)this->window_number, str);
				break;
		}
	}


	/**
	 * Some data on this window has become invalid.
	 * @param data Information about the changed data.
	 * @param gui_scope Whether the call is done from GUI scope. You may not do everything when not in GUI scope. See #InvalidateWindowData() for details.
	 */
	void OnInvalidateData(int data = 0, bool gui_scope = true) override
	{
		if (this->window_number == _local_company) return;

		if (_settings_game.economy.allow_shares) { // Shares are allowed
			const Company *c = Company::Get(this->window_number);

			/* If all shares are owned by someone (none by nobody), disable buy button */
			this->SetWidgetDisabledState(WID_C_BUY_SHARE, GetAmountOwnedBy(c, INVALID_OWNER) == 0 ||
					/* Only 25% left to buy. If the company is human, disable buying it up.. TODO issues! */
					(GetAmountOwnedBy(c, INVALID_OWNER) == 1 && !c->is_ai) ||
					/* Spectators cannot do anything of course */
					_local_company == COMPANY_SPECTATOR);

			/* If the company doesn't own any shares, disable sell button */
			this->SetWidgetDisabledState(WID_C_SELL_SHARE, (GetAmountOwnedBy(c, _local_company) == 0) ||
					/* Spectators cannot do anything of course */
					_local_company == COMPANY_SPECTATOR);
		} else { // Shares are not allowed, disable buy/sell buttons
			this->DisableWidget(WID_C_BUY_SHARE);
			this->DisableWidget(WID_C_SELL_SHARE);
		}
	}
};

static WindowDesc _company_desc(
	WDP_AUTO, "company", 0, 0,
	WC_COMPANY, WC_NONE,
	0,
	_nested_company_widgets, lengthof(_nested_company_widgets)
);

/**
 * Show the window with the overview of the company.
 * @param company The company to show the window for.
 */
void ShowCompany(CompanyID company)
{
	if (!Company::IsValidID(company)) return;

	AllocateWindowDescFront<CompanyWindow>(&_company_desc, company);
}

/**
 * Redraw all windows with company infrastructure counts.
 * @param company The company to redraw the windows of.
 */
void DirtyCompanyInfrastructureWindows(CompanyID company)
{
	SetWindowDirty(WC_COMPANY, company);
	SetWindowDirty(WC_COMPANY_INFRASTRUCTURE, company);
}

struct BuyCompanyWindow : Window {
	BuyCompanyWindow(WindowDesc *desc, WindowNumber window_number) : Window(desc)
	{
		this->InitNested(window_number);
	}

	void UpdateWidgetSize(int widget, Dimension *size, const Dimension &padding, Dimension *fill, Dimension *resize) override
	{
		switch (widget) {
			case WID_BC_FACE:
				*size = GetSpriteSize(SPR_GRADIENT);
				break;

			case WID_BC_QUESTION:
				const Company *c = Company::Get((CompanyID)this->window_number);
				SetDParam(0, c->index);
				SetDParam(1, c->bankrupt_value);
				size->height = GetStringHeight(STR_BUY_COMPANY_MESSAGE, size->width);
				break;
		}
	}

	void SetStringParameters(int widget) const override
	{
		switch (widget) {
			case WID_BC_CAPTION:
				SetDParam(0, STR_COMPANY_NAME);
				SetDParam(1, Company::Get((CompanyID)this->window_number)->index);
				break;
		}
	}

	void DrawWidget(const Rect &r, int widget) const override
	{
		switch (widget) {
			case WID_BC_FACE: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				DrawCompanyManagerFace(c->face, c->colour, r.left, r.top);
				break;
			}

			case WID_BC_QUESTION: {
				const Company *c = Company::Get((CompanyID)this->window_number);
				SetDParam(0, c->index);
				SetDParam(1, c->bankrupt_value);
				DrawStringMultiLine(r.left, r.right, r.top, r.bottom, STR_BUY_COMPANY_MESSAGE, TC_FROMSTRING, SA_CENTER);
				break;
			}
		}
	}

	void OnClick(Point pt, int widget, int click_count) override
	{
		switch (widget) {
			case WID_BC_NO:
				this->Close();
				break;

			case WID_BC_YES:
				Command<CMD_BUY_COMPANY>::Post(STR_ERROR_CAN_T_BUY_COMPANY, (CompanyID)this->window_number);
				break;
		}
	}
};

static const NWidgetPart _nested_buy_company_widgets[] = {
	NWidget(NWID_HORIZONTAL),
		NWidget(WWT_CLOSEBOX, COLOUR_LIGHT_BLUE),
		NWidget(WWT_CAPTION, COLOUR_LIGHT_BLUE, WID_BC_CAPTION), SetDataTip(STR_ERROR_MESSAGE_CAPTION_OTHER_COMPANY, STR_TOOLTIP_WINDOW_TITLE_DRAG_THIS),
	EndContainer(),
	NWidget(WWT_PANEL, COLOUR_LIGHT_BLUE),
		NWidget(NWID_VERTICAL), SetPIP(8, 8, 8),
			NWidget(NWID_HORIZONTAL), SetPIP(8, 10, 8),
				NWidget(WWT_EMPTY, INVALID_COLOUR, WID_BC_FACE), SetFill(0, 1),
				NWidget(WWT_EMPTY, INVALID_COLOUR, WID_BC_QUESTION), SetMinimalSize(240, 0), SetFill(1, 1),
			EndContainer(),
			NWidget(NWID_HORIZONTAL, NC_EQUALSIZE), SetPIP(100, 10, 100),
				NWidget(WWT_TEXTBTN, COLOUR_LIGHT_BLUE, WID_BC_NO), SetMinimalSize(60, 12), SetDataTip(STR_QUIT_NO, STR_NULL), SetFill(1, 0),
				NWidget(WWT_TEXTBTN, COLOUR_LIGHT_BLUE, WID_BC_YES), SetMinimalSize(60, 12), SetDataTip(STR_QUIT_YES, STR_NULL), SetFill(1, 0),
			EndContainer(),
		EndContainer(),
	EndContainer(),
};

static WindowDesc _buy_company_desc(
	WDP_AUTO, nullptr, 0, 0,
	WC_BUY_COMPANY, WC_NONE,
	WDF_CONSTRUCTION,
	_nested_buy_company_widgets, lengthof(_nested_buy_company_widgets)
);

/**
 * Show the query to buy another company.
 * @param company The company to buy.
 */
void ShowBuyCompanyDialog(CompanyID company)
{
	AllocateWindowDescFront<BuyCompanyWindow>(&_buy_company_desc, company);
}
