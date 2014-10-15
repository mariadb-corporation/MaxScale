### Introduction
After you run the command on the profile page a basic setup is created for you, including this
tutorial and a few other files. These files include content describing some of the details on
how to format the content.

### Adding support
If you used the script to generate the support files, you can skip this section.

In your repository to add support you'll need the `support` directory, inside this directory
you will need to create a `gitdoc.json` file. This file contains details about the repository.
```
$ mkdir support/
$ touch support/gitdoc.json
```
The other files in the `support` directory are optional, only `gitdoc.json` is required.

The following sections describe the information needed in the files.

### gitdoc.json
There are a few fields in this file that make it easy to give your viewers extra details about
the community, as well as the domain for premium repos.

#### googleGroup
This is a field you can use to give your viewers a link to your google group. Just set the field
to the name of your google group, and we'll create the link. It isn't required though, you can
leave it out safely.

#### irc
This is a field you can use to give your viewers the details to join your irc channel. To use it
set an `object` with two fields. One of the fields is `server`, this is the server address for the
irc network(e.g. `irc.freenode.net` for the freenode network). The next is the `channel` field,
this tells the viewers the specific channel for your community. The channel requires you to
include the prefix character though(e.g. #, &, +, !), since there are multiple.

#### domain
This is a field you can use if your repository is premium. It tells us the name of the domain
for your repo, so we can get your repo if we detect a custom domain. You can ignore it, or just
set a placeholder if it's not premium yet.

#### analytics
When you view the repo from a custom premium domain, if this field is given it'll include the
Google Analytics snippet to track page views. This value should be your GA web property ID.

### changelog.md
This file is just a markdown file displaying the changelog for your repository. For details on
the markdown, view the default documentation created. This file can be safely ignored if you don't
want to include a changelog page.

### faq.md
This file is just a markdown file displaying the frequently asked questions for your repository.
For details on the markdown, view the default documentation created. This file can be safely
ignored if you don't want to include a faq page.

### tutorial.md
This file is just a markdown file displaying the tutorial for your repository. For details on the
markdown, view the default documentation created. This file can be safely ignored if you don't
want to include a tutorial page.

### styles.css
This file is used when your repository is premium to display custom layouts for your viewers. It
can be safely ignored, or just as a place holder if the repo is not premium yet.

### reference/
This directory is used to hold the markdown files for the documentation/reference page. Every file
in this directory will be added as a section to the documentation with the section titled after
the file name, so make sure it's readable.

### guide/
This directory is used to hold the markdown files for the documentation/guide page. Every file in this
directory will be added as a section to the guide with the section titled after the file name, so
make sure it's readable.
