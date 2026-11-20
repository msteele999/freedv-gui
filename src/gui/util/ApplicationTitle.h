#ifndef FREEDV_APPLICATION_TITLE_H
#define FREEDV_APPLICATION_TITLE_H

#include <wx/string.h>

// Human-readable identity for this experimental build. Keep the technical
// version used by logging, reporting, packaging and release tags separate.
inline wxString GetFreeDVApplicationTitle()
{
    return wxT("FreeDV W2MWS RADEV2 Experimental v.9");
}

#endif // FREEDV_APPLICATION_TITLE_H
