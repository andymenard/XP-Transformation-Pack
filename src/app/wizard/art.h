#pragma once
// The Setup wizard's pictures, drawn here -- nothing of Windows': the tall
// one down the left of the first and last pages (a Windows Installer
// wizard's 164 x 314), and the icon at the right of the other pages' banner.
// In the spirit of the classic installer art: deep blue and black, light
// sweeping across, and a boxed product in the corner.
#include <QPixmap>

namespace xpl::wizard {

QPixmap sidePicture();          // 164 x 314
QPixmap bannerIcon(int size);   // the boxed product, `size` square

} // namespace xpl::wizard
