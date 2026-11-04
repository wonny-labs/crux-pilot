// The reference with sign-in and home only, for the size comparison with the Angular sample. The forced
// password change is built into the shell, so it stays in this build.
import '../../reference/src/style.css';
import { mountReferenceInterface } from '../../reference/src/app.tsx';
import { homeScreen } from '../../reference/src/screens/home.tsx';

const root = document.getElementById('app');
if (root !== null) {
  mountReferenceInterface(root, { screens: [homeScreen] });
}
