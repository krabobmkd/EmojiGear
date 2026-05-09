/*
 * ftmodule.h – AmigaOS custom FreeType module list.
 *
 * Only TrueType (.ttf/.ttc) and OpenType/CFF (.otf/.otc) are needed.
 * Omitted: Type1, CID, PFR, Type42, WinFNT, PCF, BDF, SDF, SVG.
 *
 * This file overrides include/freetype/config/ftmodule.h by being found
 * first in the include search path (ft_amiga_config/ is listed before
 * the FreeType include/ directory).
 */

FT_USE_MODULE( FT_Module_Class,    autofit_module_class    )
FT_USE_MODULE( FT_Driver_ClassRec, tt_driver_class         )  /* TrueType      */
FT_USE_MODULE( FT_Driver_ClassRec, cff_driver_class        )  /* OpenType/CFF  */
FT_USE_MODULE( FT_Module_Class,    psaux_module_class      )
FT_USE_MODULE( FT_Module_Class,    psnames_module_class    )
FT_USE_MODULE( FT_Module_Class,    pshinter_module_class   )
FT_USE_MODULE( FT_Module_Class,    sfnt_module_class       )
FT_USE_MODULE( FT_Renderer_Class,  ft_smooth_renderer_class  )
FT_USE_MODULE( FT_Renderer_Class,  ft_raster1_renderer_class )

/* EOF */
