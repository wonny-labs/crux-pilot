// The Preact example with home and configuration, which with the sign-in and the forced password change that the
// shell holds are its core flows, for the size comparison with the Angular sample.
import '../../../samples/web/preact/src/style.css';
import { mountPreactInterface } from '../../../samples/web/preact/src/app.tsx';
import { configScreen } from '../../../samples/web/preact/src/screens/config.tsx';
import { homeScreen } from '../../../samples/web/preact/src/screens/home.tsx';

const root = document.getElementById('app');
if (root !== null) {
  mountPreactInterface(root, { screens: [homeScreen, configScreen] });
}
