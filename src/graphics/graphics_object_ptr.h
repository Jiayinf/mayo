/****************************************************************************
** Copyright (c) 2021, Fougue Ltd. <http://www.fougue.pro>
** All rights reserved.
** See license at https://github.com/fougue/mayo/blob/master/LICENSE.txt
****************************************************************************/

#pragma once

#include "../base/occ_handle.h"
#include <AIS_InteractiveObject.hxx>
#include <TDF_Label.hxx>

namespace Mayo {

using GraphicsObjectPtr = OccHandle<AIS_InteractiveObject>;
using GraphicsObjectSelectionMode = int;

struct TGraphicsObjectExt
{
	GraphicsObjectPtr obj;
	TDF_Label lbl;
	int tnId;
	std::string name;

	TGraphicsObjectExt()
	{
		reset();
	}

	void reset()
	{
		obj.Nullify();
		tnId = -1;
		name = "";
	}
};

} // namespace Mayo
