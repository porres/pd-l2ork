#include "m_pd.h"
#include "m_imp.h"
#include "g_canvas.h"
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#ifdef _MSC_VER
#pragma warning( disable : 4244 )
#pragma warning( disable : 4305 )
#endif

/*
#ifdef _WIN32
#include <stdlib.h>
#define realpath(N,R) _fullpath((R),(N),_MAX_PATH)
#endif
*/

/* ------------------------ image ----------------------------- */

static t_class *image_class;

typedef struct _image
{
    t_object x_obj;
    t_glist *x_glist;
    int x_width;
    int x_height;
    t_symbol *x_image;
    t_symbol *x_key; // key to cache the image on the gui side
    int x_type; //0=file 1=tk_image
    t_int x_localimage; //localimage "img%x" done
    t_symbol* x_inlet; // for size callback
} t_image;

/* forward declarations */
static void image_size(t_image *x, t_floatarg w, t_floatarg h);

static int num_instances = 0;

/* widget helper functions */

static const char *image_get_filename(t_image *x, char *file)
{
    static char dirresult[MAXPDSTRING];
    char fname[FILENAME_MAX];
    char *fileresult, *fullpath;
    int fd;
    // ico@vt.edu 2021-10-19: got to expand the file to fname
    // otherwise relative path will fail to open the file on js side
    canvas_makefilename(
        glist_getcanvas(x->x_glist),
        file,
        fname, FILENAME_MAX
        );
#ifndef _WIN32
    char realfname[FILENAME_MAX];
    //post("got non-windows");
    realpath(fname, realfname);
    fd = open_via_path(canvas_getdir(glist_getcanvas(x->x_glist))->s_name,
        realfname, "", dirresult, &fileresult, MAXPDSTRING, 1);
#endif
#ifdef _WIN32
    //post("got windows");
    fd = open_via_path(canvas_getdir(glist_getcanvas(x->x_glist))->s_name,
        fname, "", dirresult, &fileresult, MAXPDSTRING, 1);
#endif
    //post("image_get_filename file=%s fname=%s realfname=%s fd=%d dirresult=%s fileresult=%s",
    //  file, fname, realfname, fd, dirresult, fileresult);

    if (fd > 0)
    {
        /* dirresult and fileresult are in the same buffer (see comment 
           for do_open_via_path in s_path.c). This means we can change
           the null terminator that separates them to a backslash to
           retrieve the full path... */
        fullpath = dirresult;
        fullpath[strlen(fullpath)] = '/';
        sys_close(fd);
        return fullpath;
    }
    else return 0;
}

// firsttime == 0, just reposition
// firsttime == 1, redraw only without loading the image (to avoid
//                 race condition between front-end and back-end)
// firsttime == 2, also do a load of the image
static void image_drawme(t_image *x, t_glist *glist, int firsttime)
{
    char key[MAXPDSTRING];
    char key2[MAXPDSTRING];
    if (firsttime)
    {
        gui_vmess("gui_gobj_new", "xxsiii",
            glist_getcanvas(glist),
            x,
            "obj",
            text_xpix(&x->x_obj, glist),
            text_ypix(&x->x_obj, glist),
            glist_istoplevel(glist));
        const char *fname = image_get_filename(x, x->x_image->s_name);
        if (x->x_image == &s_ || !fname) // if we have a blank image name, use the included filler
        {
            sprintf(key, "x%zx", (t_uint)pd_class(&x->x_obj.te_pd));
            sprintf(key2, "x%zx", (t_uint)pd_class(&x->x_obj.te_pd));
            strcat(key, key2);
            strcat(key, "default");
            //x->x_image = gensym("::moonlib::image::noimage");
            x->x_key = gensym(key);
            x->x_type = 1;
            pd_error(x, "[moonlib/image]: no image found");
        }
        if (x->x_type || !fname)
        {
            //sys_vgui(".x%zx.c create image %d %d -tags %xS\n",
            //         glist_getcanvas(glist),
            //         text_xpix(&x->x_obj, glist),
            //         text_ypix(&x->x_obj, glist),
            //         x);
            //sys_vgui(".x%zx.c itemconfigure %xS -image %s\n",
            //         glist_getcanvas(glist), x, x->x_image->s_name);
            x->x_width = 25;
            x->x_height = 25;
            gui_vmess("gui_gobj_draw_image", "xxssiiiii",
                glist_getcanvas(glist),
                x,
                key,
                "center", 0, 0, 0, 1, (glist != glist_getcanvas(glist) ? 1 : 0));
        }
        else if (fname)
        {
            sprintf(key, "x%zx", (t_uint)x);
            if (!x->x_localimage)
            {
                //sys_vgui("image create photo img%x\n", x);
                x->x_localimage = 1;
            }
            if (fname && firsttime == 2)
            {
                /* associate a filename with the image */
                //sys_vgui("::moonlib::image::configure .x%zx img%x {%s}\n",
                //    x, x, fname);
                gui_vmess("gui_moonlib_load_image", "xsssi",
                    glist_getcanvas(glist), key, fname, x->x_inlet->s_name, 2);
            }
            //sys_vgui(".x%zx.c create image %d %d -image img%x -tags %xS\n",
            //         glist_getcanvas(glist),
            //         text_xpix(&x->x_obj, glist),
            //         text_ypix(&x->x_obj, glist),
            //         x,
            //         x);
            gui_vmess("gui_gobj_draw_image", "xxssiiiii",
                glist_getcanvas(glist),
                x,
                key,
                "center", 0, 0, 0, 1, (glist != glist_getcanvas(glist) ? 1 : 0));
        }
        /* TODO callback from gui
          sys_vgui("image_size logo");
        */
        /* Finally, draw a border */
        if (x->x_glist == glist_getcanvas(x->x_glist))
            gui_vmess("gui_moonlib_image_draw_border", "xxiiiii",
                glist_getcanvas(glist),
                x,
                0,
                0,
                x->x_width,
                x->x_height,
                1);
    }
    else
    {
        //sys_vgui(".x%zx.c coords %xS %d %d\n",
        //         glist_getcanvas(glist), x,
        //         text_xpix(&x->x_obj, glist), text_ypix(&x->x_obj, glist));
        gui_vmess("gui_image_coords", "xxii",
            glist_getcanvas(glist),
            x,
            text_xpix(&x->x_obj, glist),
            text_ypix(&x->x_obj, glist));
    }
}

static void image_erase(t_image *x, t_glist *glist)
{
    //sys_vgui(".x%zx.c delete %xS\n", glist_getcanvas(glist), x);
    gui_vmess("gui_gobj_erase", "xx", glist_getcanvas(glist), x);
}


/* ------------------------ image widgetbehaviour -------------------------- */

static void image_getrect(t_gobj *z, t_glist *glist,
    int *xp1, int *yp1, int *xp2, int *yp2)
{
    int width, height;
    t_image *x = (t_image *)z;

    width = x->x_width;
    height = x->x_height;
    *xp1 = text_xpix(&x->x_obj, glist) - width/2;
    *yp1 = text_ypix(&x->x_obj, glist) - height/2;
    *xp2 = text_xpix(&x->x_obj, glist) + width/2;
    *yp2 = text_ypix(&x->x_obj, glist) + height/2;
    //post("moonlib_image_getrect %d %d %d %d", *xp1, *yp1, *xp2, *yp2);
}

static void image_displace(t_gobj *z, t_glist *glist, int dx, int dy)
{
    t_image *x = (t_image *)z;
    x->x_obj.te_xpix += dx;
    x->x_obj.te_ypix += dy;
    //sys_vgui(".x%zx.c coords %xSEL %d %d %d %d\n",
    //    glist_getcanvas(glist), x,
    //    text_xpix(&x->x_obj, glist),
    //    text_ypix(&x->x_obj, glist),
    //    text_xpix(&x->x_obj, glist) + x->x_width,
    //    text_ypix(&x->x_obj, glist) + x->x_height);
    image_drawme(x, glist, 0);
    canvas_fixlinesfor(glist,(t_text *)x);
}

static void image_displace_wtag(t_gobj *z, t_glist *glist, int dx, int dy)
{
    t_image *x = (t_image *)z;
    x->x_obj.te_xpix += dx;
    x->x_obj.te_ypix += dy;
    //sys_vgui(".x%zx.c coords %xSEL %d %d %d %d\n",
    //    glist_getcanvas(glist), x,
    //    text_xpix(&x->x_obj, glist),
    //    text_ypix(&x->x_obj, glist),
    //    text_xpix(&x->x_obj, glist) + x->x_width,
    //    text_ypix(&x->x_obj, glist) + x->x_height);
    //image_drawme(x, glist, 0);
    canvas_fixlinesfor(glist,(t_text *)x);
}

static void image_select(t_gobj *z, t_glist *glist, int state)
{
    t_image *x = (t_image *)z;
    //if (state)
    //{
    //    sys_vgui(".x%zx.c create rectangle "
    //             "%d %d %d %d -tags %xSEL -outline blue\n",
    //        glist_getcanvas(glist),
    //        text_xpix(&x->x_obj, glist), text_ypix(&x->x_obj, glist),
    //        text_xpix(&x->x_obj, glist) + x->x_width,
    //        text_ypix(&x->x_obj, glist) + x->x_height,
    //        x);
    //}
    //else
    //{
    //    sys_vgui(".x%zx.c delete %xSEL\n",
    //        glist_getcanvas(glist), x);
    //}
    gui_vmess("gui_image_toggle_border", "xxi",
        glist_getcanvas(glist), x, state);
    if (state)
        gui_vmess("gui_gobj_select", "xx", glist_getcanvas(glist), x);
    else
        gui_vmess("gui_gobj_deselect", "xx", glist_getcanvas(glist), x);
}

static void image_activate(t_gobj *z, t_glist *glist, int state)
{
    /*    t_text *x = (t_text *)z;
        t_rtext *y = glist_findrtext(glist, x);
        if (z->g_pd != gatom_class) rtext_activate(y, state);*/
}

static void image_delete(t_gobj *z, t_glist *glist)
{
    t_text *x = (t_text *)z;
    canvas_deletelinesfor(glist, x);
}

static void image_vis(t_gobj *z, t_glist *glist, int vis)
{
    t_image *s = (t_image *)z;
    if (vis)
        image_drawme(s, glist, 2);
    else
        image_erase(s, glist);
}

/* can we use the normal text save function ?? */

static void image_save(t_gobj *z, t_binbuf *b)
{
    t_image *x = (t_image *)z;
    binbuf_addv(b, "ssiissi",
                gensym("#X"),
                gensym("obj"),
                x->x_obj.te_xpix,
                x->x_obj.te_ypix,
                atom_getsymbol(binbuf_getvec(x->x_obj.te_binbuf)),
                (x->x_image != &s_ ? x->x_image : gensym("null")),
                x->x_type);
    binbuf_addv(b, ";");
}

static t_widgetbehavior image_widgetbehavior;

// this is now exclusively used for the callback from the GUI
// once the image has been loaded
static void image_size(t_image *x, t_floatarg w, t_floatarg h)
{
    //post("moonlib_image_size");
    x->x_width = w;
    x->x_height = h;
    image_erase(x, x->x_glist);
    image_drawme(x, x->x_glist, 1);
}

static void image_color(t_image *x, t_symbol *col)
{
    /*     outlet_bang(x->x_obj.ob_outlet); only bang if there was a bang ..
           so color black does the same as bang, but doesn't forward the bang
    */
}

static void image_open(t_gobj *z, t_symbol *file)
{
    t_image *x = (t_image *)z;
    const char *fname;
    char key[MAXPDSTRING];
    int oldtype = x->x_type;
    fname = image_get_filename(x, file->s_name);
    if (fname)
    {
        sprintf(key, "x%zx", (t_uint)x);
        x->x_image = file;
        x->x_key = gensym(key);
        x->x_type = 0;
        if (glist_isvisible(x->x_glist))
        {
            if (!x->x_localimage)
            {
                //sys_vgui("image create photo img%x\n", x);
                x->x_localimage = 1;
            }
            //sys_vgui("img%x blank\n", x);
            //sys_vgui("::moonlib::image::configure .x%zx img%x {%s}\n",
            //    x, x, fname);
            gui_vmess("gui_moonlib_load_image", "xsssi",
                glist_getcanvas(x->x_glist), key, fname, x->x_inlet->s_name, 2);
            //gui_vmess("gui_load_image", "xssi",
            //    glist_getcanvas(x->x_glist), key, fname, 2);
            /*
            if (oldtype == 0)
            {
                //sys_vgui(".x%zx.c itemconfigure %xS -image img%x\n",
                //    glist_getcanvas(x->x_glist), x, x);
            }
            */
        }
    }
    else
        pd_error(x, "[moonlib/image]: error opening file '%s'", file->s_name);
}

static void image_load(t_gobj *z, t_symbol *image, t_symbol *file)
{
    t_image *x = (t_image *)z;
    const char *fname;
    char key[MAXPDSTRING];
    fname = image_get_filename(x, file->s_name);
    //post("image_load %s %s", file->s_name, fname);
    if (fname)
    {
        //sys_vgui("::moonlib::image::create_photo .x%zx %s {%s}\n",
        //    x, image->s_name, fname);
        /* For these class-accessible names, we prefix the user-provided
           name with a class pointer. */
        sprintf(key, "x%zx", (t_uint)pd_class(&x->x_obj.te_pd));
        strcat(key, image->s_name);
        gui_vmess("gui_load_image", "xssi",
            glist_getcanvas(x->x_glist), key, fname, 2);
    }
    else
    {
        pd_error(x, "[moonlib/image]: can't load %s", image->s_name);
    }
}

static void image_set(t_gobj *z, t_symbol *image)
{
    char key[MAXPDSTRING];
    t_image *x = (t_image *)z;
    /* key is the class address followed by the user-supplied string */
    sprintf(key, "x%zx", (t_uint)pd_class(&x->x_obj.te_pd));
    strcat(key, image->s_name);
    x->x_image = image;
    x->x_key = gensym(key);
    x->x_type = 1;
    if (glist_isvisible(x->x_glist))
    {
        //sys_vgui(".x%zx.c itemconfigure %xS -image %s\n",
        //         glist_getcanvas(x->x_glist), x, x->x_image->s_name);
        gui_vmess("gui_image_configure", "xxss",
            glist_getcanvas(x->x_glist),
            x,
            key,
            "center");
    }
}

static void image_setwidget(void)
{
    image_widgetbehavior.w_getrectfn = image_getrect;
    image_widgetbehavior.w_displacefn = image_displace;
    image_widgetbehavior.w_selectfn = image_select;
    image_widgetbehavior.w_activatefn = image_activate;
    image_widgetbehavior.w_deletefn = image_delete;
    image_widgetbehavior.w_visfn = image_vis;
#if (PD_VERSION_MINOR > 31)
    image_widgetbehavior.w_clickfn = NULL;
    image_widgetbehavior.w_propertiesfn = NULL;
#endif
#if PD_MINOR_VERSION < 37
    image_widgetbehavior.w_savefn = image_save;
#endif
    image_widgetbehavior.w_displacefnwtag = image_displace_wtag;
}

static void image_free(t_image *x)
{
    if (x->x_inlet)
    {
        pd_unbind(&x->x_obj.ob_pd, x->x_inlet);
    }
    num_instances--;
    //post("moonlib/image num_instances=%d", num_instances);
    if (!num_instances)
    {
        // we just deleted the last moonlib/image object, so we will now free
        // all of the images created for the moonlib object
        gui_vmess("gui_image_free_loaded_images", "i", 2);
    }
}

static void *image_new(t_symbol *image, t_float type)
{
    post("WARNING: moonlib/image is a defunct and unsupported object. It is"
         " included purely for legacy reasons. Please use ggee/image or simply"
         " image instead. It provides all the moonlib/image functionality and more"
         " without the limitations of this object.");
    t_image *x = (t_image *)pd_new(image_class);
    char key[MAXPDSTRING];
    x->x_glist = (t_glist *)canvas_getcurrent();
    x->x_width = 15;
    x->x_height = 15;
    if (type != 0)
        x->x_type= 1;
    else
        x->x_type= 0;
    x->x_localimage = 0;
    if (strcmp(image->s_name, "null"))
    {
        if (x->x_type)
        {
            sprintf(key, "x%zx", (t_uint)pd_class(&x->x_obj.te_pd));
            strcat(key, image->s_name);
            x->x_image = image;
            x->x_key = gensym(key);
        }
        else
        {
            x->x_image = image;
        }
    }
    else
        x->x_image = &s_;

    char buf[MAXPDSTRING];
    sprintf(buf, "#%zx", (t_uint)x);
    x->x_inlet = gensym(buf);
    pd_bind(&x->x_obj.ob_pd, x->x_inlet);

    outlet_new(&x->x_obj, &s_float);
    num_instances++;
    return (x);
}

void image_setup(void)
{
    image_class = class_new(gensym("image"), (t_newmethod)image_new, 
            (t_method)image_free, sizeof(t_image), 0, A_DEFSYM, A_DEFFLOAT, 0);
    
    class_addmethod(image_class, (t_method)image_size, gensym("_imagesize"),
    	A_FLOAT, A_FLOAT, 0);
    /*
        class_addmethod(image_class, (t_method)image_color, gensym("color"),
        	A_SYMBOL, 0);
    */
    class_addmethod(image_class, (t_method)image_open, gensym("open"),
                    A_SYMBOL, 0);
    class_addmethod(image_class, (t_method)image_set, gensym("set"),
                    A_SYMBOL, 0);
    class_addmethod(image_class, (t_method)image_load, gensym("load"),
                    A_SYMBOL, A_SYMBOL, 0);
    image_setwidget();
    class_setwidget(image_class, &image_widgetbehavior);
#if PD_MINOR_VERSION >= 37
    class_setsavefn(image_class, &image_save);
#endif
    /* cache a default image (question mark) for case where no image argument
       is given. The key is ("x%zxx%zxdefault", image_class, image_class),
       to protect against namespace clashes with the complicated interface
       of moonlib/image */
    char key[MAXPDSTRING];
    char key2[MAXPDSTRING];
    sprintf(key, "x%zx", (t_uint)image_class);
    sprintf(key2, "x%zx", (t_uint)image_class);
    strcat(key, key2);
    strcat(key, "default");
    gui_vmess("gui_load_default_image", "ss", "dummy", key);
}
