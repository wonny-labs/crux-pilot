// The reference with home and configuration, which with the sign-in and the forced password change that the
// shell holds are its core flows, for the size comparison with the Angular sample.
import '../../reference/src/style.css';
import { mountReferenceInterface } from '../../reference/src/app.tsx';
import { configScreen } from '../../reference/src/screens/config.tsx';
import { homeScreen } from '../../reference/src/screens/home.tsx';

const root = document.getElementById('app');
if (root !== null) {
  mountReferenceInterface(root, { screens: [homeScreen, configScreen] });
}
