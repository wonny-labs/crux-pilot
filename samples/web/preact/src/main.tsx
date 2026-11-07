// The Preact interface as Crux ships it. A project with its own needs writes its own version of this
// file: see docs/plans/web-frontend-implementation.md.
import './style.css';
import { mountPreactInterface } from './app.tsx';
import { configScreen } from './screens/config.tsx';
import { homeScreen } from './screens/home.tsx';
import { networkScreen } from './screens/network.tsx';
import { rebootScreen } from './screens/reboot.tsx';

const root = document.getElementById('app');
if (root !== null) {
  mountPreactInterface(root, { screens: [homeScreen, configScreen, networkScreen, rebootScreen] });
}
