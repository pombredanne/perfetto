import * as m from 'mithril';

export default {
    view () {
        return m('.trackshell',
            { style: { border: "1px solid #ccc", padding: "20px" } }, [
            m('h1', "Track Shell")
        ]);
    }
} as m.Component;