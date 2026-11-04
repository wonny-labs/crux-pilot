// The reference interface as Crux ships it. A project with its own needs writes its own version of this
// file: see docs/plans/web-frontend-implementation.md.
import './style.css';
import { mountReferenceInterface } from './app.tsx';
import { homeScreen } from './screens/home.tsx';

const root = document.getElementById('app');
if (root !== null) {
  mountReferenceInterface(root, { screens: [homeScreen] });
}
