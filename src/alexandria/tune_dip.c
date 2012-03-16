/* -*- mode: c; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; c-file-style: "stroustrup"; -*-
 * $Id: tune_dip.c,v 1.39 2009/04/12 21:24:26 spoel Exp $
 * 
 *                This source code is part of
 * 
 *                 G   R   O   M   A   C   S
 * 
 *          GROningen MAchine for Chemical Simulations
 * 
 *                        VERSION 4.0.99
 * Written by David van der Spoel, Erik Lindahl, Berk Hess, and others.
 * Copyright (c) 1991-2000, University of Groningen, The Netherlands.
 * Copyright (c) 2001-2008, The GROMACS development team,
 * check out http://www.gromacs.org for more information.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * If you want to redistribute modifications, please consider that
 * scientific software is very special. Version control is crucial -
 * bugs must be traceable. We will be happy to consider code for
 * inclusion in the official distribution, but derived work must not
 * be called official GROMACS. Details are found in the README & COPYING
 * files - if they are missing, get the official version at www.gromacs.org.
 * 
 * To help us fund GROMACS development, we humbly ask that you cite
 * the papers on the package - you can find them in the top README file.
 * 
 * For more info, check our website at http://www.gromacs.org
 * 
 * And Hey:
 * Groningen Machine for Chemical Simulation
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#ifdef GMX_LIB_MPI
#include <mpi.h>
#endif
#ifdef GMX_THREADS
#include "tmpi.h"
#endif
#include "maths.h"
#include "macros.h"
#include "copyrite.h"
#include "bondf.h"
#include "string2.h"
#include "smalloc.h"
#include "strdb.h"
#include "sysstuff.h"
#include "confio.h"
#include "physics.h"
#include "futil.h"
#include "statutil.h"
#include "vec.h"
#include "3dview.h"
#include "txtdump.h"
#include "readinp.h"
#include "names.h"
#include "vec.h"
#include "atomprop.h"
#include "xvgr.h"
#include "mdatoms.h"
#include "force.h"
#include "vsite.h"
#include "shellfc.h"
#include "network.h"
#include "viewit.h"
#include "gmx_random.h"
#include "gmx_wallcycle.h"
#include "gmx_statistics.h"
#include "convparm.h"
#include "gpp_atomtype.h"
#include "grompp.h"
#include "gen_ad.h"
#include "slater_integrals.h"
#include "gentop_qgen.h"
#include "gentop_core.h"
#include "poldata.h"
#include "poldata_xml.h"
#include "molprop.h"
#include "molprop_xml.h"
#include "molprop_util.h"
#include "molselect.h"
#include "mtop_util.h"
#include "gentop_comm.h"
#include "nmsimplex.h"

enum { eSupportNo, eSupportLocal, eSupportRemote, eSupportNR };

typedef struct {
    char           *molname,*lot,*ref;
    int            eSupport;
    int            qtotal,mult,natom,nalloc,nshell;
    real           dip_exp,mu_exp2,dip_err,dip_weight,dip_calc,chieq;
    real           *qESP;
    gmx_mtop_t     mtop;
    gmx_localtop_t *ltop;
    t_symtab       symtab;
    t_inputrec     ir;
    gmx_shellfc_t  shell;
    t_mdatoms      *md;
    t_atoms        *atoms;
    t_forcerec     *fr;
    gmx_vsite_t    *vs;
    gmx_resp_t     gr;
    int            *symmetric_charges;
    rvec           *x,*f,*buf,mu_exp,mu_calc,mu_esp,coq;
    tensor         Q_exp,Q_calc,Q_esp;
    t_state        state;
    matrix         box;
    gentop_qgen_t  qgen;
} t_mymol; 

typedef struct {
    int  n,nopt,nconst,nopt_c;
    char **name;
    int  *tot_count,*count;
    gmx_bool *bConst;
} t_index_count;

enum { ermsBOUNDS, ermsMU, ermsQUAD, ermsCHARGE, ermsESP, ermsTOT, ermsNR };

typedef struct {
    gmx_bool    bDone,bFinal,bGaussianBug,bFitZeta;
    int     nmol,nmol_support,iModel;
    t_mymol *mymol;
    t_index_count *ic;
    real    J0_0,Chi0_0,w_0,J0_1,Chi0_1,w_1,hfac,hfac0,decrzeta,epsr;
    real    ener[ermsNR],fc[ermsNR];
    gmx_bool    bOptHfac,bPol,bQM;
    char    *fixchi;
    gmx_poldata_t  pd;
    gmx_atomprop_t atomprop;
    t_commrec *cr;
} t_moldip;

#define gmx_assert(n,m) if (n != m) { gmx_fatal(FARGS,"Variable %s = %d, should have been %d",#n,n,m); }

static void add_index_count(t_index_count *ic,char *name,gmx_bool bConst)
{
    int i;
  
    for(i=0; (i<ic->n); i++) 
        if (strcasecmp(ic->name[i],name) == 0) {
            if (ic->bConst[i] != bConst)
                gmx_fatal(FARGS,"Trying to add atom %s as both constant and optimized",
                          name);
            else
                fprintf(stderr,"Trying to add %s twice\n",name);
        }
    if (i == ic->n) {
        ic->n++;
        srenew(ic->name,ic->n);
        srenew(ic->tot_count,ic->n);
        srenew(ic->count,ic->n);
        srenew(ic->bConst,ic->n);
        ic->name[i] = strdup(name);
        ic->tot_count[i] = 0;
        ic->count[i] = 0;
        ic->bConst[i] = bConst;
        if (bConst)
            ic->nconst++;
        else
            ic->nopt++;
    }
}

static void sum_index_count(t_index_count *ic,t_commrec *cr)
{
    int i;
    
    for(i=0; (i<ic->n); i++)
        ic->tot_count[i] = ic->count[i];
    if (cr->nnodes > 1)
        gmx_sumi(ic->n,ic->tot_count,cr);
}

static void inc_index_count(t_index_count *ic,char *name)
{
    int i;
  
    for(i=0; (i<ic->n); i++) 
        if (strcasecmp(ic->name[i],name) == 0) {
            ic->count[i]++;
            break;
        }
    if (i == ic->n)
        gmx_fatal(FARGS,"No such atom %s",name);
}

static void dec_index_count(t_index_count *ic,char *name)
{
    int i;
  
    for(i=0; (i<ic->n); i++) 
        if (strcasecmp(ic->name[i],name) == 0) {
            if (ic->count[i] > 0) {
                ic->count[i]--;
                break;
            }
            else
                gmx_fatal(FARGS,"Trying to decrease number of atoms %s below zero",
                          name);
        }
    if (i == ic->n)
        gmx_fatal(FARGS,"No such atom %s",name);
}

static int n_index_count(t_index_count *ic,char *name) 
{
    int i;
  
    for(i=0; (i<ic->n); i++) {
        if (strcasecmp(ic->name[i],name) == 0)
            return i;
    }
    return -1;
}

static int c_index_count(t_index_count *ic,char *name) 
{
    int i;
  
    for(i=0; (i<ic->n); i++) {
        if (strcasecmp(ic->name[i],name) == 0)
            return ic->tot_count[i];
    }
    return 0;
}

static char *opt_index_count(t_index_count *ic) 
{
    for(; (ic->nopt_c < ic->n); ic->nopt_c++)
        if (!ic->bConst[ic->nopt_c]) {
            return ic->name[ic->nopt_c++];
        }
    ic->nopt_c = 0;
  
    return NULL;
}

static gmx_bool const_index_count(t_index_count *ic,char *name) 
{
    int i;
  
    for(i=0; (i<ic->n); i++) {
        if (strcasecmp(ic->name[i],name) == 0)
            return ic->bConst[i];
    }
    return FALSE;
}

static void dump_index_count(t_index_count *ic,FILE *fp,
                             int iModel,gmx_poldata_t pd,
                             gmx_bool bFitZeta)
{
    int i,j,nZeta,nZopt;
    double zz;
    if (fp) {
        fprintf(fp,"Atom index for this optimization.\n");
        fprintf(fp,"Name  Number  Action   #Zeta\n");
        for(i=0; (i<ic->n); i++) {
            nZeta = gmx_poldata_get_nzeta(pd,iModel,ic->name[i]);
            nZopt = 0;
            for(j=0; (j<nZeta); j++)
            {
                zz = gmx_poldata_get_zeta(pd,iModel,ic->name[i],j);
                if (zz > 0)
                    nZopt++;
            }
            if (ic->bConst[i])
                fprintf(fp,"%-4s  %6d  Constant\n",ic->name[i],ic->count[i]);
            else
                fprintf(fp,"%-4s  %6d  Optimized %4d%s\n",
                        ic->name[i],ic->count[i],nZopt,
                        bFitZeta ? " optimized" : " constant");
        }
        fprintf(fp,"\n");
        fflush(fp);
    }
}

static int clean_index_count(t_index_count *ic,int minimum_data,FILE *fp)
{
    int i,j,nremove=0;
  
    for(i=0; (i<ic->n); ) {
        if (!ic->bConst[i] && (ic->tot_count[i] < minimum_data)) {
            if (fp)
                fprintf(fp,"Not enough support in data set for optimizing %s\n",
                        ic->name[i]);
            sfree(ic->name[i]);
            for(j=i; (j<ic->n-1); j++) {
                ic->name[j]   = ic->name[j+1];
                ic->count[j]  = ic->count[j+1];
                ic->tot_count[j]  = ic->tot_count[j+1];
                ic->bConst[j] = ic->bConst[j+1];
            }
            nremove++;
            ic->n--;
            ic->nopt--;
        }
        else 
            i++;
    }
    return nremove;
}

static gmx_bool is_symmetric(t_mymol *mymol,real toler)
{
    int  i,j,m;
    real mm,tm;
    rvec com,test;
    gmx_bool *bSymm,bSymmAll;
  
    clear_rvec(com);
    tm = 0;
    for(i=0; (i<mymol->atoms->nr); i++) {
        mm  = mymol->atoms->atom[i].m;
        tm += mm; 
        for(m=0; (m<DIM); m++) {
            com[m] += mm*mymol->x[i][m];
        }
    }
    for(m=0; (m<DIM); m++) 
        com[m] /= tm;
  
    for(i=0; (i<mymol->atoms->nr); i++) 
        rvec_dec(mymol->x[i],com);
    
    snew(bSymm,mymol->atoms->nr);
    for(i=0; (i<mymol->atoms->nr); i++) {
        bSymm[i] = (norm(mymol->x[i]) < toler);
        for(j=i+1; (j<mymol->atoms->nr) && !bSymm[i]; j++) {
            rvec_add(mymol->x[i],mymol->x[j],test);
            if (norm(test) < toler) {
                bSymm[i] = TRUE;
                bSymm[j] = TRUE;
            }
        }
    }
    bSymmAll = TRUE;
    for(i=0; (i<mymol->atoms->nr); i++) {
        bSymmAll = bSymmAll && bSymm[i];
    }
    sfree(bSymm);
  
    return bSymmAll;
}

enum 
{ 
    immOK, immZeroDip, immNoQuad, immCharged, immError, 
    immAtomTypes, immAtomNumber, immMolpropConv,
    immQMInconsistency, immTest, immNR 
};

const char *immsg[immNR] = 
{ 
    "OK", "Zero Dipole", "No Quadrupole", "Charged", "Error", 
    "Atom type problem", "Atom number problem", "Converting from molprop",
    "QM Inconsistency (ESP dipole does not match Elec)", 
    "Not in training set"
};

static void do_init_mtop(gmx_mtop_t *mtop,int ntype,int nmoltype,
                         int natoms,t_atoms **atoms)
{
    init_mtop(mtop);

    if (nmoltype <= 0)
    {
        gmx_incons("Number of moltypes less than 1 in do_init_mtop");
    } 
    mtop->nmoltype = nmoltype;
    snew(mtop->moltype,mtop->nmoltype);
    mtop->nmolblock = nmoltype;
    snew(mtop->molblock,mtop->nmolblock);
    mtop->molblock[0].nmol = 1;
    mtop->molblock[0].type = 0;
    mtop->molblock[0].natoms_mol = natoms;
    
    /* Create a charge group block */
    stupid_fill_block(&(mtop->moltype[0].cgs),natoms,FALSE);
    /* Create an exclusion block */
    stupid_fill_blocka(&(mtop->moltype[0].excls),natoms);
    
    mtop->natoms = natoms;
    init_t_atoms(&(mtop->moltype[0].atoms),natoms,FALSE);
    *atoms = &(mtop->moltype[0].atoms);

    if (ntype <= 0)
    {
        gmx_incons("Number of atomtypes less than 1 in do_init_mtop");
    } 
    mtop->ffparams.atnr=ntype;
    snew(mtop->ffparams.functype,mtop->ffparams.atnr);
    snew(mtop->ffparams.iparams,ntype*ntype);
    mtop->ffparams.functype[0] = F_LJ;
    /* Add more initiation stuff here */
    
}

void excls_to_blocka(int natom,t_excls excls[],t_blocka *blocka)
{
    int i,j,k,nra;
    
    if (blocka->nr < natom)
    {
        srenew(blocka->index,natom+1);
    }
    nra = 0;
    for(i=0; (i<natom); i++)
        nra += excls[i].nr;
    snew(blocka->a,nra+1);
    nra = 0;
    for(i=j=0; (i<natom); i++)
    {
        blocka->index[i] = nra;
        for(k=0; (k<excls[i].nr); k++)
            blocka->a[j++] = excls[i].e[k];
        nra += excls[i].nr;
    }
    blocka->index[natom] = nra;
}

void plist_to_mtop(t_params plist[],gmx_mtop_t *mtop)
{
    int i,j,k,l,nra,n=0;
    
    for(i=0; (i<F_NRE); i++)
    {
        nra = interaction_function[i].nratoms;
        snew(mtop->moltype[0].ilist[i].iatoms,plist[i].nr*(nra+1));
        srenew(mtop->ffparams.functype,mtop->ffparams.ntypes+plist[i].nr);
        srenew(mtop->ffparams.iparams,mtop->ffparams.ntypes+plist[i].nr);
        k = 0;
        for(j=0; (j<plist[i].nr); j++)
        {
            n = enter_params(&mtop->ffparams,i,plist[i].param[j].c,0,12,n,TRUE);
            mtop->moltype[0].ilist[i].iatoms[k++] = n;
            for(l=0; (l<nra); l++)
                mtop->moltype[0].ilist[i].iatoms[k++] = plist[i].param[j].a[l];
        }
        mtop->moltype[0].ilist[i].nr = k;
    }
}

void mtop_update_cgs(gmx_mtop_t *mtop)
{
    int i,j;
    
    for(i=0; (i<mtop->nmoltype); i++) 
    {
        if (mtop->moltype[i].atoms.nr > mtop->moltype[i].cgs.nr)
        {
            mtop->moltype[i].cgs.nr = mtop->moltype[i].atoms.nr;
            mtop->moltype[i].cgs.nalloc_index = mtop->moltype[i].atoms.nr+1;
            srenew(mtop->moltype[i].cgs.index,mtop->moltype[i].cgs.nr+1);
            for(j=0; (j<=mtop->moltype[i].cgs.nr); j++)
                mtop->moltype[i].cgs.index[j] = j;
        }
    }
}
            
static int init_mymol(t_mymol *mymol,gmx_molprop_t mp,
                      gmx_bool bQM,char *lot,gmx_bool bZero,
                      gmx_poldata_t pd,gmx_atomprop_t aps,
                      int  iModel,t_commrec *cr,int *nwarn,
                      gmx_bool bCharged,const output_env_t oenv,
                      real th_toler,real ph_toler,
                      real dip_toler,real hfac,gmx_bool bESP,
                      real watoms,real rDecrZeta,gmx_bool bPol,gmx_bool bFitZeta)
{
    int      i,j,m,version,generation,step,*nbonds,tatomnumber,imm=immOK;
    char     **smnames,*mylot,*myref;
    rvec     xmin,xmax;
    tensor   quadrupole;
    double   value,error,vec[3];
    gpp_atomtype_t atype;
    gentop_vsite_t gvt;
    real     btol = 0.2;
    t_pbc    pbc;
    t_excls  *newexcls;
    t_params plist[F_NRE];
      
    init_plist(plist);
    mymol->qtotal  = gmx_molprop_get_charge(mp);
    mymol->mult    = gmx_molprop_get_multiplicity(mp);
    mymol->natom   = gmx_molprop_get_natom(mp);
    if (mymol->natom <= 0)
        imm = immAtomTypes;
    else
    {
        if (bPol)
            mymol->nalloc = mymol->natom*2+2;
        else
            mymol->nalloc = mymol->natom;
        
        if ((mymol->qtotal != 0) && !bCharged)
            imm = immCharged;
    }
    if (immOK == imm) 
    {
        mymol->molname  = strdup(gmx_molprop_get_molname(mp));
        /* Read coordinates */
        do_init_mtop(&mymol->mtop,1,1,mymol->natom,&(mymol->atoms));
        open_symtab(&(mymol->symtab));
        if (molprop_2_atoms(mp,aps,&(mymol->symtab),lot,mymol->atoms,
                            (const char *)"ESP",&(mymol->x)) == 0)
            imm = immMolpropConv;
    }
    if (immOK == imm)
    {
        clear_rvec(xmin);
        clear_rvec(xmax);
        clear_rvec(mymol->coq);
        tatomnumber = 0;
        snew(mymol->qESP,mymol->nalloc);
        for(i=0; (i<mymol->atoms->nr); i++) 
        {
            mymol->qESP[i] = mymol->atoms->atom[i].q;
            for(m=0; (m<DIM); m++)
            {
                if (mymol->x[i][m] < xmin[m])
                    xmin[m] = mymol->x[i][m];
                else if (mymol->x[i][m] > xmax[m])
                    xmax[m] = mymol->x[i][m];
                mymol->coq[m] += mymol->x[i][m]*mymol->atoms->atom[i].atomnumber;
            }
            tatomnumber += mymol->atoms->atom[i].atomnumber;
        }
        if (tatomnumber > 0)
        {
            for(m=0; (m<DIM); m++)
                mymol->coq[m] /= tatomnumber;
            for(i=0; (i<mymol->atoms->nr); i++) 
                rvec_dec(mymol->x[i],mymol->coq);
        }
        else
        {
            imm = immAtomNumber;
        }
    }
    if (immOK == imm) 
    {
        if (!bZero && is_symmetric(mymol,0.01)) 
        {
            imm = immZeroDip;
        }
    }
    if (immOK == imm)
    {   
        clear_mat(mymol->box);
        for(m=0; (m<DIM); m++)
            mymol->box[m][m] = 2*(xmax[m]-xmin[m]) + 1;
        
        snew(nbonds,mymol->nalloc);
        snew(smnames,mymol->nalloc);
        mk_bonds(pd,mymol->atoms,mymol->x,NULL,&(plist[F_BONDS]),nbonds,
                 TRUE,mymol->box,aps,btol);
    
        /* Setting the atom types: this depends on the bonding */
        set_pbc(&pbc,epbcNONE,mymol->box);
        gvt = gentop_vsite_init(egvtLINEAR);
        if ((atype = set_atom_type(NULL,mymol->molname,&(mymol->symtab),mymol->atoms,
                                   &(plist[F_BONDS]),nbonds,smnames,pd,aps,
                                   mymol->x,&pbc,th_toler,ph_toler,gvt)) == NULL) 
            imm = immAtomTypes;
        gentop_vsite_done(&gvt);
        close_symtab(&(mymol->symtab));
    }
    if (immOK == imm)
    {
        sfree(nbonds);
        /* Move plist into idef: not implemented yet. */
        mymol->ltop     = gmx_mtop_generate_local_top(&(mymol->mtop),&(mymol->ir));
        mymol->eSupport = eSupportLocal;
        if (mp_get_prop_ref(mp,empDIPOLE,(bQM ? iqmQM : iqmBoth),
                            lot,NULL,(char *)"elec",
                            &value,&error,&myref,&mylot,
                            vec,quadrupole) == 0)
        {
            imm = immZeroDip;
            sfree(myref);
            sfree(mylot);
        }
        else
        {
            mymol->dip_exp  = value;
            mymol->dip_err  = error;
            mymol->lot      = mylot;
            mymol->ref      = myref;
            for(m=0; (m<DIM); m++)
            {
                mymol->mu_exp[m] = vec[m];
            }
            mymol->mu_exp2 = sqr(value);
            if (error <= 0) {
                if (debug)
                    fprintf(debug,"WARNING: Error for %s is %g, assuming it is 10%%.\n",
                            gmx_molprop_get_molname(mp),error);
                (*nwarn)++;
                error = 0.1*value;
            }
            mymol->dip_weight = sqr(1.0/error);
        }
        if (mp_get_prop_ref(mp,empDIPOLE,(bQM ? iqmQM : iqmBoth),
                            lot,NULL,(char *)"ESP",&value,&error,NULL,NULL,vec,quadrupole) != 0)
        {
            for(m=0; (m<DIM); m++)
            {
                mymol->mu_esp[m] = vec[m];
            }
        }
    }
    if (immOK == imm)
    {
        rvec dx;
        rvec_sub(mymol->mu_esp,mymol->mu_exp,dx);
        if (norm(dx) > dip_toler)
        {
            imm = immQMInconsistency;
        }
    }
    if (immOK == imm)
    {
        if (bQM)
        {
            if (mp_get_prop_ref(mp,empQUADRUPOLE,iqmQM,
                                lot,NULL,(char *)"elec",&value,&error,
                                NULL,NULL,vec,quadrupole) == 0)
                imm = immNoQuad;
            else
                copy_mat(quadrupole,mymol->Q_exp);
            if (mp_get_prop_ref(mp,empQUADRUPOLE,iqmQM,
                                lot,NULL,(char *)"ESP",&value,&error,
                                NULL,NULL,vec,quadrupole) != 0)
                copy_mat(quadrupole,mymol->Q_esp);
        }
    }
    if ((immOK == imm) && bESP)
    {
        char *xyz_unit,*V_unit;
        double x,y,z,V;
        int  cref,xu,vu,espnr;
        
        snew(mymol->symmetric_charges,mymol->nalloc);
        mymol->symmetric_charges = symmetrize_charges(TRUE,mymol->atoms,
                                                      &(plist[F_BONDS]),pd,aps,(char *)"");
        
        mymol->gr = gmx_resp_init(pd,iModel,FALSE,0,0,mymol->qtotal,
                                  0,0,-1,TRUE,watoms,rDecrZeta,FALSE,1,
                                  bFitZeta,FALSE,NULL);
        if (NULL != mymol->gr)
        {
            gmx_resp_add_atom_info(mymol->gr,mymol->atoms,pd);
            gmx_resp_update_atomtypes(mymol->gr,mymol->atoms);
            gmx_resp_add_atom_symmetry(mymol->gr,pd,
                                       mymol->symmetric_charges);
            gmx_resp_add_atom_coords(mymol->gr,mymol->x);
            if (0 != (cref = gmx_molprop_get_calc_lot(mp,lot))) 
            {
                while (gmx_molprop_get_potential(mp,cref,&xyz_unit,
                                                 &V_unit,&espnr,&x,&y,&z,&V) == 1)
                {
                    /* Maybe not convert to gmx ? */
                    xu = string2unit(xyz_unit);
                    vu = string2unit(V_unit);
                    if (-1 == xu)
                        xu = eg2cAngstrom;
                    if (-1 == vu)
                        vu = eg2cHartree_e;
                    gmx_resp_add_point(mymol->gr,
                                       convert2gmx(x,xu),
                                       convert2gmx(y,xu),
                                       convert2gmx(z,xu),
                                       convert2gmx(V,vu));
                    sfree(xyz_unit);
                    sfree(V_unit);
                }
            }
        }
        else
            imm = immError;                          
    }
    else 
    {
        mymol->gr = NULL;
    }
    if (immOK == imm)
    {
        if (bPol) {
            mymol->vs = init_vsite(&mymol->mtop,cr);
            snew(mymol->f,mymol->nalloc);
            snew(mymol->buf,mymol->nalloc);
            snew(newexcls,mymol->nalloc);
            srenew(mymol->x,mymol->nalloc);
            add_shells(pd,mymol->nalloc,&(mymol->mtop.moltype[0].atoms),
                       atype,plist,mymol->x,&mymol->symtab,
                       &newexcls,smnames);
            mymol->mtop.natoms = mymol->mtop.moltype[0].atoms.nr;
            mymol->mtop.molblock[0].natoms_mol = mymol->mtop.natoms;
            excls_to_blocka(mymol->nalloc,newexcls,&(mymol->mtop.moltype[0].excls));
            mtop_update_cgs(&mymol->mtop);
            plist_to_mtop(plist,&mymol->mtop);
            reset_q(mymol->atoms);
            mymol->shell = init_shell_flexcon(debug,&mymol->mtop,0,mymol->x);
            mymol->fr = mk_forcerec();
            init_forcerec(debug,oenv,mymol->fr,NULL,&mymol->ir,&mymol->mtop,cr,
                          mymol->box,FALSE,NULL,NULL,NULL,NULL,NULL, TRUE,-1);
        }
        else 
            mymol->shell = NULL;
  
        init_state(&mymol->state,mymol->atoms->nr,1,1,1);
        mymol->md = init_mdatoms(debug,&mymol->mtop,FALSE);
    }
    if (immOK != imm) 
    {
        mymol->qgen = NULL; 
    }
    if (immOK != imm) 
    {
        /* Remove temporary data */
    }
    else if (NULL != debug)
    {
        fprintf(debug,"Succesfully added %s\n",mymol->molname);
    }
    return imm;
}

static void print_stats(FILE *fp,const char *prop,gmx_stats_t lsq,gmx_bool bHeader,
                        char *xaxis,char *yaxis)
{
    real a,da,b,db,chi2,rmsd,Rfit;
    int  n;
    
    if (bHeader)
    {
        fprintf(fp,"Fitting data to y = ax+b, where x = %s and y = %s\n",
                xaxis,yaxis);
        fprintf(fp,"%-12s %5s %13s %13s %8s %8s\n",
                "Property","N","a","b","R","RMSD");
        fprintf(fp,"---------------------------------------------------------------\n");
    }
    gmx_stats_get_ab(lsq,elsqWEIGHT_NONE,&a,&b,&da,&db,&chi2,&Rfit);
    gmx_stats_get_rmsd(lsq,&rmsd);
    gmx_stats_get_npoints(lsq,&n);
    fprintf(fp,"%-12s %5d %6.3f(%5.3f) %6.3f(%5.3f) %7.2f%% %8.4f\n",
            prop,n,a,da,b,db,Rfit*100,rmsd);
}

static void print_lsq_set(FILE *fp,gmx_stats_t lsq)
{
    real   x,y;
    
    fprintf(fp,"@type xy\n");
    while (gmx_stats_get_point(lsq,&x,&y,NULL,NULL,0) == estatsOK)
    {
        fprintf(fp,"%10g  %10g\n",x,y);
    }
    fprintf(fp,"&\n");
}

static void print_quad(FILE *fp,tensor Q_exp,tensor Q_calc,char *calc_name,
                       real q_toler)
{
    tensor dQ;
    real delta;
    if (NULL != calc_name) 
    {
        m_sub(Q_exp,Q_calc,dQ);
        delta = sqrt(sqr(dQ[XX][XX])+sqr(dQ[XX][YY])+sqr(dQ[XX][ZZ])+
                     sqr(dQ[YY][YY])+sqr(dQ[YY][ZZ]));
        fprintf(fp,
                "%-4s (%6.2f %6.2f %6.2f) Dev: (%6.2f %6.2f %6.2f) Delta: %6.2f %s\n"
                "     (%6s %6.2f %6.2f)      (%6s %6.2f %6.2f)\n",
                calc_name,
                Q_calc[XX][XX],Q_calc[XX][YY],Q_calc[XX][ZZ],
                dQ[XX][XX],dQ[XX][YY],dQ[XX][ZZ],delta,(delta > q_toler) ? "YYY" : "",
                "",Q_calc[YY][YY],Q_calc[YY][ZZ],
                "",dQ[YY][YY],dQ[YY][ZZ]);
    }
    else
    {
        fprintf(fp,"Quadrupole analysis (5 independent components only)\n");
        fprintf(fp,
                "Exp  (%6.2f %6.2f %6.2f)\n"
                "     (%6s %6.2f %6.2f)\n",
                Q_exp[XX][XX],Q_exp[XX][YY],Q_exp[XX][ZZ],
                "",Q_exp[YY][YY],Q_exp[YY][ZZ]);
    }
}

static void print_dip(FILE *fp,rvec mu_exp,rvec mu_calc,char *calc_name,
                      real toler)
{
    rvec dmu;
    real ndmu,cosa;
    char ebuf[32];
    
    if (NULL != calc_name) 
    {
        rvec_sub(mu_exp,mu_calc,dmu);
        ndmu = norm(dmu);
        cosa = cos_angle(mu_exp,mu_calc);
        if (ndmu > toler)
            sprintf(ebuf,"XXX");
        else if (fabs(cosa) < 0.1)
            sprintf(ebuf,"YYY");
        else
            ebuf[0] = '\0';
        fprintf(fp,"%-4s (%6.2f,%6.2f,%6.2f) |Mu| = %5.2f Dev: (%6.2f,%6.2f,%6.2f) |%5.2f|%s\n",
                calc_name,
                mu_calc[XX],mu_calc[YY],mu_calc[ZZ],norm(mu_calc),
                dmu[XX],dmu[YY],dmu[ZZ],ndmu,ebuf);
    }
    else
    {
        fprintf(fp,"Dipole analysis\n");
        fprintf(fp,"Exp  (%6.2f,%6.2f,%6.2f) |Mu| = %5.2f\n",
                mu_exp[XX],mu_exp[YY],mu_exp[ZZ],norm(mu_exp));
    }
}

static void xvgr_symbolize(FILE *xvgf,int nsym,const char *leg[],
                           const output_env_t oenv)
{
    int i;

    xvgr_legend(xvgf,nsym,leg,oenv);
    for(i=0; (i<nsym); i++)
    {
        xvgr_line_props(xvgf,i,elNone,ecBlack+i,oenv);
        fprintf(xvgf,"@ s%d symbol %d\n",i,i+1);
    }        
}

static void print_mols(FILE *fp,const char *xvgfn,const char *qhisto,
                       const char *cdiff,const char *mudiff,const char *Qdiff,
                       const char *espdiff,
                       int nmol,t_mymol mol[],
                       gmx_poldata_t pd,int iModel,real hfac,
                       real dip_toler,real quad_toler,real q_toler,output_env_t oenv)
{
    FILE   *xvgf,*qdiff,*mud,*tdiff,*hh,*espd;
    double d2=0;
    real   rms,sigma,aver,error,xESP,xEEM,qq,chi2,espx,espy,espdx,espdy,wtot;
    int    i,j,k,n,nout,nlsqt=0,mm,nn,nesp;
    char   *resnm,*atomnm;
    const  char **atomtypes=NULL;
    enum { eprEEM, eprESP, eprNR };
    gmx_stats_t lsq_q,lsq_mu[eprNR],lsq_quad[eprNR],*lsqt=NULL,lsq_esp;
    const char *eprnm[eprNR] = { "EEM", "ESP" };
    gmx_mtop_atomloop_all_t aloop;
    t_atom *atom;     
    int    at_global,resnr;
   
    xvgf  = xvgropen(xvgfn,"Correlation between dipoles",
                     "Experimental","Predicted",oenv);
    xvgr_symbolize(xvgf,2,eprnm,oenv);
    lsq_q       = gmx_stats_init();
    lsq_quad[0] = gmx_stats_init();
    lsq_quad[1] = gmx_stats_init();
    lsq_mu[0]   = gmx_stats_init();
    lsq_mu[1]   = gmx_stats_init();
    lsq_esp     = gmx_stats_init();
    for(i=n=0; (i<nmol); i++) {
        if (mol[i].eSupport != eSupportNo) {
            fprintf(fp,"Molecule %d: %s. Qtot: %d, Multiplicity %d\n",
                    n+1, mol[i].molname,mol[i].qtotal,mol[i].mult);
            fprintf(fp,"Data source: %s (%s)\n",mol[i].lot,mol[i].ref);
            
            print_dip(fp,mol[i].mu_exp,NULL,NULL,dip_toler);
            print_dip(fp,mol[i].mu_exp,mol[i].mu_calc,(char *)"EEM",dip_toler);
            print_dip(fp,mol[i].mu_exp,mol[i].mu_esp,(char *)"ESP",dip_toler);
            
            print_quad(fp,mol[i].Q_exp,NULL,NULL,quad_toler);
            print_quad(fp,mol[i].Q_exp,mol[i].Q_calc,(char *)"EEM",quad_toler);
            print_quad(fp,mol[i].Q_exp,mol[i].Q_esp,(char *)"ESP",quad_toler);
            chi2 = gmx_resp_get_rms(mol[i].gr,&wtot);
            fprintf(fp,"ESP chi2 %g Hartree/e wtot = %g\n",chi2,wtot);
            gmx_resp_pot_lsq(mol[i].gr,lsq_esp);
            while (estatsOK == gmx_stats_get_point(lsq_esp,&espx,&espy,
                                                   &espdx,&espdy,5))
            {
                fprintf(fp,"ESP outlier: EEM = %g, should be %g\n",espy,espx);
            }
            
            fprintf(xvgf,"%10g  %10g\n",mol[i].dip_exp,mol[i].dip_calc);
            for(mm=0; (mm<DIM); mm++) {
                gmx_stats_add_point(lsq_mu[0],mol[i].mu_exp[mm],mol[i].mu_calc[mm],0,0);
                gmx_stats_add_point(lsq_mu[1],mol[i].mu_exp[mm],mol[i].mu_esp[mm],0,0);
                if (0) {
                    for(nn=mm; (nn<DIM); nn++) {
                        if (mm < ZZ) {
                            gmx_stats_add_point(lsq_quad[0],mol[i].Q_exp[mm][nn],
                                                mol[i].Q_calc[mm][nn],0,0);
                            gmx_stats_add_point(lsq_quad[1],mol[i].Q_exp[mm][nn],
                                                mol[i].Q_esp[mm][nn],0,0);
                        }
                    }
                }
                else {
                    /* Ignore off-diagonal components */
                    gmx_stats_add_point(lsq_quad[0],mol[i].Q_exp[mm][mm],
                                        mol[i].Q_calc[mm][mm],0,0);
                    gmx_stats_add_point(lsq_quad[1],mol[i].Q_exp[mm][mm],
                                        mol[i].Q_esp[mm][mm],0,0);
                 
                }
            }

            d2 += sqr(mol[i].dip_exp-mol[i].dip_calc);
            fprintf(fp,"Atom   Type      q_EEM     q_ESP       x       y       z\n");
            aloop = gmx_mtop_atomloop_all_init(&mol[i].mtop);
            j = 0;
            while (gmx_mtop_atomloop_all_next(aloop,&at_global,&atom)) {
                gmx_mtop_atomloop_all_names(aloop,&atomnm,&resnr,&resnm);
                for(k=0; (k<nlsqt); k++) 
                    if (strcmp(atomtypes[k],*(mol[i].atoms->atomtype[j])) == 0)
                        break;
                if (k == nlsqt) 
                {
                    srenew(lsqt,++nlsqt);
                    srenew(atomtypes,nlsqt);
                    atomtypes[k] = strdup(*(mol[i].atoms->atomtype[j]));
                    lsqt[k] = gmx_stats_init();
                }
                qq = atom->q;
                fprintf(fp,"%-2s%3d  %-5s  %8.4f  %8.4f%8.3f%8.3f%8.3f %s\n",
                        atomnm,j+1,
                        *(mol[i].atoms->atomtype[j]),qq,mol[i].qESP[j],
                        mol[i].x[j][XX],mol[i].x[j][YY],mol[i].x[j][ZZ],
                        fabs(qq-mol[i].qESP[j]) > q_toler ? "ZZZ" : "");
                gmx_stats_add_point(lsqt[k],mol[i].qESP[j],atom->q,0,0);
                gmx_stats_add_point(lsq_q,mol[i].qESP[j],atom->q,0,0);
                j++;
            }
            gmx_assert(j,mol[i].atoms->nr);
            fprintf(fp,"\n");
            n++;
        }
    }
    fclose(xvgf);

    print_stats(fp,(char *)"dipoles",lsq_mu[0],TRUE,(char *)"Elec",(char *)"EEM");
    print_stats(fp,(char *)"quadrupoles",lsq_quad[0],FALSE,(char *)"Elec",(char *)"EEM");
    print_stats(fp,(char *)"charges",lsq_q,FALSE,(char *)"ESP",(char *)"EEM");
    print_stats(fp,(char *)"esp",lsq_esp,FALSE,(char *)"Elec",(char *)"EEM");
    fprintf(fp,"\n");
    
    print_stats(fp,(char *)"dipoles",lsq_mu[1],TRUE,(char *)"Elec",(char *)"ESP");
    print_stats(fp,(char *)"quadrupoles",lsq_quad[1],FALSE,(char *)"Elec",(char *)"ESP");

    mud = xvgropen(mudiff,"Correlation between Mu Elec and others",
                   "muElec","mu",oenv);
    xvgr_symbolize(mud,2,eprnm,oenv);
    print_lsq_set(mud,lsq_mu[0]);
    print_lsq_set(mud,lsq_mu[1]);
    fclose(mud);
    
    espd = xvgropen(espdiff,"Correlation between Esp Elec and others",
                   "ESP (Hartree/e)","ESP (Hartree/e)",oenv);
    xvgr_symbolize(espd,2,eprnm,oenv);
    print_lsq_set(espd,lsq_esp);
    fclose(espd);
    
    tdiff = xvgropen(Qdiff,"Correlation between Theta Elec and others",
                     "thetaElec","theta",oenv);
    xvgr_symbolize(tdiff,2,eprnm,oenv);
    print_lsq_set(tdiff,lsq_quad[0]);
    print_lsq_set(tdiff,lsq_quad[1]);
    fclose(tdiff);
    qdiff = xvgropen(cdiff,"Correlation between ESP and EEM","qESP","qEEM",oenv);
    xvgr_legend(qdiff,nlsqt,atomtypes,oenv);
    xvgr_symbolize(qdiff,nlsqt,atomtypes,oenv);
    hh = xvgropen(qhisto,"Histogram for charges","q (e)","a.u.",oenv);
    xvgr_legend(hh,nlsqt,atomtypes,oenv);
    fprintf(fp,"\nDeviations of the charges separated per atomtype:\n");
    for(k=0; (k<nlsqt); k++) 
    {
        int N;
        real *x,*y;
        
        print_stats(fp,atomtypes[k],lsqt[k],(k == 0),(char *)"ESP",
                    (char *)"EEM");
        print_lsq_set(qdiff,lsqt[k]);
        if (gmx_stats_get_npoints(lsqt[k],&N) == estatsOK) 
        {
            N = N/4;
            if (gmx_stats_make_histogram(lsqt[k],0,&N,ehistoY,0,&x,&y) == estatsOK)
            {
                fprintf(hh,"@type xy\n");
                for(i=0; (i<N); i++)
                {
                    fprintf(hh,"%10g  %10g\n",x[i],y[i]);
                }
                fprintf(hh,"&\n");
                sfree(x);
                sfree(y);
            }
        }
    }
    fclose(qdiff);
    fclose(hh);
        
    rms = sqrt(d2/n);
    fprintf(fp,"RMSD = %.3f D\n",rms);
    fprintf(fp,"hfac = %g\n",hfac);
    gmx_stats_get_ase(lsq_mu[0],&aver,&sigma,&error);
    sigma = rms;
    nout  = 0;
    fprintf(fp,"Overview of outliers (> %.3f off)\n",2*sigma);
    fprintf(fp,"----------------------------------\n");
    fprintf(fp,"%-20s  %12s  %12s  %12s\n",
            "Name","Predicted","Experimental","Mu-Deviation");
    for(i=0; (i<nmol); i++) {
        rvec dmu;
        rvec_sub(mol[i].mu_exp,mol[i].mu_calc,dmu);
        if ((mol[i].eSupport != eSupportNo) &&
            (mol[i].dip_exp > sigma) && 
            (norm(dmu) > 2*sigma)) {
            fprintf(fp,"%-20s  %12.3f  %12.3f  %12.3f\n",
                    mol[i].molname,mol[i].dip_calc,mol[i].dip_exp,
                    mol[i].dip_calc-mol[i].dip_exp);
            nout ++;
        }
    }
    if (nout)
        printf("There were %d outliers. See at the very bottom of the log file\n",
               nout);
    else
        printf("No outliers! Well done.\n");
    do_view(oenv,xvgfn,NULL);
  
    gmx_stats_done(lsq_q);
    gmx_stats_done(lsq_quad[0]);
    gmx_stats_done(lsq_quad[1]);
    gmx_stats_done(lsq_mu[0]);
    gmx_stats_done(lsq_mu[1]);
    sfree(lsq_q);
    sfree(lsq_quad[0]);
    sfree(lsq_quad[1]);
    sfree(lsq_mu[0]);
    sfree(lsq_mu[1]);
}

static int check_data_sufficiency(FILE *fp,int nmol,t_mymol mol[],
                                  int minimum_data,gmx_poldata_t pd,
                                  t_index_count *ic,gmx_atomprop_t aps,
                                  int iModel,char *opt_elem,char *const_elem,
                                  t_commrec *cr,gmx_bool bPol,
                                  gmx_bool bFitZeta)
{
    int i,j,nremove,nsupported;
    gmx_mtop_atomloop_all_t aloop;
    t_atom *atom;
    char   **ptr,*myname;
    int    k,at_global,mymodel,resnr;
  
    /* Parse opt_elem list to test which elements to optimize */
    if (const_elem) {
        ptr = split(' ',const_elem);
        for(k=0; (ptr[k]); k++) {
            if (gmx_poldata_have_eem_support(pd,iModel,ptr[k],FALSE)) 
                add_index_count(ic,ptr[k],TRUE);
        }
    }
    if (opt_elem) {
        ptr = split(' ',opt_elem);
        for(k=0; (ptr[k]); k++) {
            if (gmx_poldata_have_eem_support(pd,iModel,ptr[k],TRUE)) 
                add_index_count(ic,ptr[k],FALSE);
        } 
    }
    else {
        while (gmx_poldata_get_eemprops(pd,&mymodel,&myname,NULL,NULL,NULL,NULL,NULL) != 0) {
            if ((mymodel == iModel) &&
                !const_index_count(ic,myname) &&
                gmx_poldata_have_eem_support(pd,iModel,myname,FALSE))
                add_index_count(ic,myname,FALSE);
        }
    }
    sum_index_count(ic,cr);
    dump_index_count(ic,debug,iModel,pd,bFitZeta);
    for(i=0; (i<nmol); i++) {
        if (mol[i].eSupport != eSupportNo) 
        {
            aloop = gmx_mtop_atomloop_all_init(&mol[i].mtop);
            k = 0;
            while (gmx_mtop_atomloop_all_next(aloop,&at_global,&atom) &&
                   (mol[i].eSupport != eSupportNo)) 
            {
                if ((atom->atomnumber > 0) || !bPol)
                {
                    if (n_index_count(ic,*(mol[i].atoms->atomtype[k])) == -1) 
                    {
                        if (debug)
                            fprintf(debug,"Removing %s because of lacking support for atom %s\n",
                                    mol[i].molname,*(mol[i].atoms->atomtype[k]));
                        mol[i].eSupport = eSupportNo;
                    }
                }
                k++;
            }
            if (mol[i].eSupport != eSupportNo) {
                gmx_assert(k,mol[i].atoms->nr);
                aloop = gmx_mtop_atomloop_all_init(&mol[i].mtop);
                k = 0;
                while (gmx_mtop_atomloop_all_next(aloop,&at_global,&atom)) 
                {
                    if ((atom->atomnumber > 0) || !bPol)
                        inc_index_count(ic,*(mol[i].atoms->atomtype[k]));
                    k++;
                }
                gmx_assert(k,mol[i].atoms->nr);
            }
        }
    }
    do {
        sum_index_count(ic,cr);
        dump_index_count(ic,debug,iModel,pd,bFitZeta);
        nremove = 0;
        for(i=0; (i<nmol); i++) {
            if (mol[i].eSupport != eSupportNo) {
                j = 0;
                aloop = gmx_mtop_atomloop_all_init(&mol[i].mtop);
                while (gmx_mtop_atomloop_all_next(aloop,&at_global,&atom)) {
                    if (c_index_count(ic,*(mol[i].atoms->atomtype[j])) < minimum_data) {
                        if (debug)
                            fprintf(debug,"Removing %s because of no support for name %s\n",
                                    mol[i].molname,*(mol[i].atoms->atomtype[j]));
                        break;
                    }
                    j++;
                }
                if (j < mol[i].mtop.natoms) {
                    aloop = gmx_mtop_atomloop_all_init(&mol[i].mtop);
                    k = 0;
                    while (gmx_mtop_atomloop_all_next(aloop,&at_global,&atom)) 
                        dec_index_count(ic,*(mol[i].atoms->atomtype[k++]));
                    mol[i].eSupport = eSupportNo;
                    nremove++;
                }
            }
        }
        if (cr->nnodes > 1) 
        {
            /* Sum nremove */
            gmx_sumi(1,&nremove,cr);
        }
    } while (nremove > 0);
    nremove = clean_index_count(ic,minimum_data,debug);
    sum_index_count(ic,cr);
    dump_index_count(ic,fp,iModel,pd,bFitZeta);
    
    nsupported = 0;
    for(i=0; (i<nmol); i++) 
    {
        if (mol[i].eSupport == eSupportLocal)
        {
            if (NULL != debug)
                fprintf(debug,"Supported molecule %s on CPU %d\n",
                        mol[i].molname,cr->nodeid);
            nsupported++;
        }
    }
    if (cr->nnodes > 1) 
    {
        gmx_sumi(1,&nsupported,cr);
    }
    if (fp)
    {
        fprintf(fp,"Removed %d atomtypes\n",nremove);
        fprintf(fp,"There are %d supported molecules left.\n\n",nsupported);
    }
    return nsupported;
}

t_moldip *read_moldip(FILE *fp,t_commrec *cr,const char *fn,const char *pd_fn,
                      real J0_0,real Chi0_0,real w_0,
                      real J0_1,real Chi0_1,real w_1,
                      real fc_bound,real fc_mu,real fc_quad,real fc_charge,
                      real fc_esp,int  iModel,
                      char *fixchi,int minimum_data,
                      gmx_bool bZero,gmx_bool bWeighted,
                      gmx_bool bOptHfac,real hfac,
                      char *opt_elem,char *const_elem,
                      gmx_bool bQM,char *lot,gmx_bool bCharged,
                      output_env_t oenv,gmx_molselect_t gms,
                      real th_toler,real ph_toler,real dip_toler,
                      gmx_bool bGaussianBug,real watoms,real rDecrZeta,
                      gmx_bool bPol,gmx_bool bFitZeta,real epsr)
{
    char     **strings,buf[STRLEN];
    int      i,j,n,kk,nstrings,nwarn=0,nzero=0,nmol_cpu;
    t_moldip *md;
    double   dip,dip_err,dx,dy,dz;
    rvec     mu;
    int      nmol,imm,imm_count[immNR];
    gmx_molprop_t *mp=NULL;
    char     *molname;
#ifdef GMX_MPI
    MPI_Status status;
#endif
    
    snew(md,1);
    md->cr       = cr;
    md->bQM      = bQM;
    md->bDone    = FALSE;
    md->bFinal   = FALSE;
    md->bGaussianBug = bGaussianBug;
    md->bFitZeta = bFitZeta;
    md->iModel   = iModel;
    md->decrzeta = rDecrZeta;
    md->epsr     = epsr;
    for(imm = 0; (imm<immNR); imm++)
        imm_count[imm] = 0;
        
    /* Read the EEM parameters */
    md->atomprop   = gmx_atomprop_init();
    
    /* Force field data */
    if ((md->pd = gmx_poldata_read(pd_fn,md->atomprop)) == NULL)
        gmx_fatal(FARGS,"Can not read the force field information. File %s missing or incorrect.",pd_fn);
    
    if ((n = gmx_poldata_get_numprops(md->pd,iModel)) == 0)
        gmx_fatal(FARGS,"File %s does not contain the requested parameters",pd_fn);
    
    if (NULL != fp)
    {  
        fprintf(fp,"There are %d atom types in the input file %s:\n---\n",
                n,pd_fn);
        fprintf(fp,"---\n\n");
    }
    
    /* Read other stuff */
    if (MASTER(cr))
    {
        /* Now read the molecules */
        mp = gmx_molprops_read(fn,&nmol);
        for(i=0; (i<nmol); i++) 
            gmx_molprop_check_consistency(mp[i]);
        nmol_cpu = nmol/cr->nnodes + 1;
    }
    else {
        nmol_cpu = 0;
    }
    if (PAR(cr))
        gmx_sumi(1,&nmol_cpu,cr);
    if (MASTER(cr))
        snew(md->mymol,nmol);
    else
        snew(md->mymol,nmol_cpu);
    
    if (MASTER(cr)) 
    {
        for(i=n=0; (i<nmol); i++)
        {
            if (imsTrain == gmx_molselect_status(gms,gmx_molprop_get_iupac(mp[i])))
            {
                int dest = (n % cr->nnodes);
                
                imm = init_mymol(&(md->mymol[n]),mp[i],bQM,lot,bZero,
                                 md->pd,md->atomprop,
                                 iModel,md->cr,&nwarn,bCharged,oenv,
                                 th_toler,ph_toler,dip_toler,md->hfac,
                                 (fc_esp > 0),watoms,rDecrZeta,bPol,bFitZeta);
                if (immOK == imm)
                {
                    if (dest > 0)
                    {
                        md->mymol[n].eSupport = eSupportRemote;
                        /* Send another molecule */
                        gmx_send_int(cr,dest,1);
                        gmx_molprop_send(cr,dest,mp[i]);
                        imm = gmx_recv_int(cr,dest);
                        if (imm != immOK) 
                            fprintf(stderr,"Molecule %s was not accepted on node %d - error %s\n",
                                    md->mymol[n].molname,dest,immsg[imm]);
                    }
                    else
                        md->mymol[n].eSupport = eSupportLocal;
                    if (immOK == imm)
                        n++;
                }
                if ((immOK != imm) && (NULL != debug))
                {
                    fprintf(debug,"IMM: Dest: %d %s - %s\n",
                            dest,gmx_molprop_get_molname(mp[i]),immsg[imm]);
                }
            }
            else
                imm = immTest;
            imm_count[imm]++;
            /* Dispose of the molecule */
            gmx_molprop_delete(mp[i]);
        }
        /* Send signal done with transferring molecules */
        for(i=1; (i<cr->nnodes); i++) 
        {
            gmx_send_int(cr,i,0);
        }
    }
    else 
    {
        n = 0;
        while (gmx_recv_int(cr,0) == 1) 
        {
            /* Receive another molecule */
            gmx_molprop_t mpnew = gmx_molprop_receive(cr,0);
            imm = init_mymol(&(md->mymol[n]),mpnew,bQM,lot,bZero,
                             md->pd,md->atomprop,
                             iModel,md->cr,&nwarn,bCharged,oenv,
                             th_toler,ph_toler,dip_toler,md->hfac,
                             (fc_esp > 0),watoms,rDecrZeta,bPol,bFitZeta);
            md->mymol[n].eSupport = eSupportLocal;
            imm_count[imm]++;
            if (immOK == imm)
            {
                n++;
            }
            gmx_send_int(cr,0,imm);
            /* Dipose of the molecules */
            gmx_molprop_delete(mpnew);
        }
    }
    md->nmol = n;
    
    if (fp)
    {
        fprintf(fp,"There were %d warnings because of zero error bars.\n",nwarn);
        fprintf(fp,"Made topologies for %d out of %d molecules.\n",n,
                (MASTER(cr)) ? nmol : nmol_cpu);
        for(i=0; (i<immNR); i++)
            if (imm_count[i] > 0)
                fprintf(fp,"%d molecules - %s.\n",imm_count[i],immsg[i]);
        if (imm_count[immOK] != nmol)
        {
            fprintf(fp,"Check %s.debug for more information.\nYou may have to use the -debug 1 flag.\n\n",ShortProgram());
        }
    }
    snew(md->ic,1);
    md->nmol_support =
        check_data_sufficiency(MASTER(cr) ? fp : NULL,md->nmol,md->mymol,
                               minimum_data,md->pd,md->ic,md->atomprop,
                               md->iModel,opt_elem,const_elem,cr,
                               bPol,md->bFitZeta);
    if (md->nmol_support == 0)
        gmx_fatal(FARGS,"No support for any molecule!");
    md->J0_0       = J0_0;
    md->Chi0_0     = Chi0_0;
    md->w_0        = w_0;
    md->J0_1       = J0_1;
    md->Chi0_1     = Chi0_1;
    md->w_1        = w_1;
    md->fc[ermsMU]     = fc_mu;
    md->fc[ermsBOUNDS] = fc_bound;
    md->fc[ermsQUAD]   = fc_quad;
    md->fc[ermsCHARGE] = fc_charge;
    md->fc[ermsESP]    = fc_esp;
    md->fixchi     = strdup(fixchi);
    md->hfac       = hfac;	  
    md->hfac0      = hfac;	  
    md->bOptHfac   = bOptHfac;
    md->bPol       = bPol;
    
    return md;
}

static void mymol_calc_multipoles(t_mymol *mol,int iModel,gmx_bool bGaussianBug)
{
    int i,m;
    rvec mu,mm;
    real r2,dfac,q;
    gmx_mtop_atomloop_all_t aloop;
    t_atom *atom;     
    int    at_global,resnr;
    rvec   coq;
    
    clear_rvec(mu);
    aloop = gmx_mtop_atomloop_all_init(&mol->mtop);
    i = 0;
    clear_mat(mol->Q_calc);
    if (bGaussianBug)
        copy_rvec(mol->coq,coq);
    else
        clear_rvec(coq);
    while (gmx_mtop_atomloop_all_next(aloop,&at_global,&atom)) {
        q = atom->q;
        svmul(ENM2DEBYE*q,mol->x[i],mm);
        rvec_inc(mu,mm);
        
        dfac = q*0.5*10*ENM2DEBYE;
        r2   = iprod(mol->x[i],mol->x[i]);
        for(m=0; (m<DIM); m++)
            mol->Q_calc[m][m] += dfac*(3*sqr(mol->x[i][m]) - r2);
        mol->Q_calc[XX][YY] += dfac*3*(mol->x[i][XX]+coq[XX])*(mol->x[i][YY]+coq[YY]);
        mol->Q_calc[XX][ZZ] += dfac*3*(mol->x[i][XX]+coq[XX])*(mol->x[i][ZZ]+coq[ZZ]);
        mol->Q_calc[YY][ZZ] += dfac*3*(mol->x[i][YY]+coq[YY])*(mol->x[i][ZZ]+coq[ZZ]);
        
        i++;
    }
    gmx_assert(i,mol->atoms->nr);
    copy_rvec(mu,mol->mu_calc);
    mol->dip_calc = norm(mu);
}

static void split_shell_charges(gmx_mtop_t *mtop,t_idef *idef)
{
    int i,k,tp,ai,aj;
    real q,Z;
    gmx_mtop_atomloop_all_t aloop;
    t_atom *atom,*atom_i,*atom_j;     
    int    at_global,resnr;
  
    for(k=0; (k<idef->il[F_POLARIZATION].nr); ) {
        tp = idef->il[F_POLARIZATION].iatoms[k++];
        ai = idef->il[F_POLARIZATION].iatoms[k++];
        aj = idef->il[F_POLARIZATION].iatoms[k++];
    
        gmx_mtop_atomnr_to_atom(mtop,ai,&atom_i);
        gmx_mtop_atomnr_to_atom(mtop,aj,&atom_j);
    
        if ((atom_i->ptype == eptAtom) &&
            (atom_j->ptype == eptShell)) {
            q = atom_i->q;
            Z = atom_i->atomnumber;
            atom_i->q = Z;
            atom_j->q = q-Z;
        }
        else if ((atom_i->ptype == eptAtom) &&
                 (atom_j->ptype == eptShell)) {
            q = atom_j->q;
            Z = atom_j->atomnumber;
            atom_j->q = Z;
            atom_i->q = q-Z;
        }
        else
            gmx_incons("Polarization entry does not have one atom and one shell");
    }
    q = 0;
    aloop = gmx_mtop_atomloop_all_init(mtop);
    while (gmx_mtop_atomloop_all_next(aloop,&at_global,&atom)) 
        q += atom->q;
    Z = gmx_nint(q);
    if (fabs(q-Z) > 1e-3) {
        gmx_fatal(FARGS,"Total charge in molecule is not zero, but %f",q-Z);
    }
}

static void calc_moldip_deviation(t_moldip *md)
{
    int    i,j,count,atomnr;
    double qq,qtot,rr2,ener[ermsNR],etot[ermsNR];
    real   t = 0;
    rvec   mu_tot = {0,0,0};
    gmx_enerdata_t *epot;
    tensor force_vir={{0,0,0},{0,0,0},{0,0,0}};
    t_nrnb   my_nrnb;
    gmx_wallcycle_t wcycle;
    gmx_bool     bConverged;
    t_mymol *mymol;
    int     eQ;
    gmx_mtop_atomloop_all_t aloop;
    t_atom *atom; 
    int    at_global,resnr;
    
    if (PAR(md->cr)) 
    {
        gmx_bcast(sizeof(md->bDone),&md->bDone,md->cr);
        gmx_bcast(sizeof(md->bFinal),&md->bFinal,md->cr);
    }
    if (md->bDone)
        return;
    if (PAR(md->cr)) 
    {
        gmx_poldata_comm_eemprops(md->pd,md->cr);
    }
    init_nrnb(&my_nrnb);
    snew(epot,1);
  
    wcycle  = wallcycle_init(stdout,0,md->cr,1);
    for(j=0; (j<ermsNR); j++)
    {
        etot[j] = 0;
        md->ener[j] = 0;
    }
    for(i=0; (i<md->nmol); i++) {
        mymol = &(md->mymol[i]);
        if ((mymol->eSupport == eSupportLocal) ||
            (md->bFinal && (mymol->eSupport == eSupportRemote)))
        {
            /* Reset energies */
            for(j=0; (j<ermsNR); j++)
                ener[j] = 0;
            
            if (NULL == mymol->qgen)
                mymol->qgen =
                    gentop_qgen_init(md->pd,mymol->atoms,md->atomprop,
                                     mymol->x,md->iModel,md->hfac,
                                     mymol->qtotal,md->epsr);
            /*if (strcmp(mymol->molname,"1-butene") == 0)
              fprintf(stderr,"Ready for %s\n",mymol->molname);*/
            eQ = generate_charges_sm(debug,mymol->qgen,
                                     md->pd,mymol->atoms,
                                     mymol->x,1e-4,100,md->atomprop,
                                     md->hfac,
                                     &(mymol->chieq));
            if (eQ != eQGEN_OK)
            {
                char buf[STRLEN];
                qgen_message(mymol->qgen,STRLEN,buf,NULL);
                fprintf(stderr,"%s\n",buf);
            }
            else {
                aloop = gmx_mtop_atomloop_all_init(&mymol->mtop);
                j = 0;
                while (gmx_mtop_atomloop_all_next(aloop,&at_global,&atom)) {
                    atom->q = mymol->atoms->atom[j].q;
                    j++;
                }
                gmx_assert(j,mymol->atoms->nr);
            }
            
            /* Now optimize the shell positions */
            if (mymol->shell) {
                split_shell_charges(&mymol->mtop,&mymol->ltop->idef);
                atoms2md(&mymol->mtop,&(mymol->ir),0,NULL,0,
                         mymol->mtop.natoms,mymol->md);
                count = 
                    relax_shell_flexcon(debug,md->cr,FALSE,0,
                                        &(mymol->ir),TRUE,
                                        GMX_FORCE_ALLFORCES,FALSE,
                                        mymol->ltop,NULL,NULL,NULL,
                                        NULL,&(mymol->state),
                                        mymol->f,force_vir,mymol->md,
                                        &my_nrnb,wcycle,NULL,
                                        &(mymol->mtop.groups),
                                        mymol->shell,mymol->fr,FALSE,t,mu_tot,
                                        mymol->mtop.natoms,&bConverged,NULL,NULL);
            }
            /* Compute the molecular dipole */
            mymol_calc_multipoles(mymol,md->iModel,md->bGaussianBug);

            /* Compute the ESP on the points */
            if ((NULL != mymol->gr) && md->bQM)
            {
                /*gmx_resp_add_atom_info(mymol->gr,&(mymol->atoms),md->pd);*/
                gmx_resp_fill_zeta(mymol->gr,md->pd);
                gmx_resp_fill_q(mymol->gr,mymol->atoms);
                gmx_resp_calc_pot(mymol->gr);
            }
            qtot = 0;
            for(j=0; (j<mymol->atoms->nr); j++) {
                atomnr = mymol->atoms->atom[j].atomnumber;
                qq     = mymol->atoms->atom[j].q;
                qtot  += qq;
                if (((qq < 0) && (atomnr == 1)) || 
                    ((qq > 0) && ((atomnr == 8)  || (atomnr == 9) || 
                                  (atomnr == 16) || (atomnr == 17) ||
                                  (atomnr == 35) || (atomnr == 53)))) {
                    ener[ermsBOUNDS] += fabs(qq);
                }
                if (md->bQM) 
                {
                    ener[ermsCHARGE] += sqr(qq-mymol->qESP[j]);
                }
            }
            if (0 && (fabs(qtot-mymol->qtotal) > 1e-2))
                fprintf(stderr,"Warning qtot for %s is %g, should be %d\n",
                        mymol->molname,qtot,mymol->qtotal);
            if (md->bQM) 
            {
                int mm,nn;
                rvec dmu;
                real wtot;
                
                rvec_sub(mymol->mu_calc,mymol->mu_exp,dmu);
                ener[ermsMU]   = iprod(dmu,dmu);
                for(mm=0; (mm<DIM); mm++) 
                {
                    if (0) 
                    {
                        for(nn=0; (nn<DIM); nn++)
                            ener[ermsQUAD] += sqr(mymol->Q_exp[mm][nn] - mymol->Q_calc[mm][nn]);
                    }
                    else {
                        ener[ermsQUAD] += sqr(mymol->Q_exp[mm][mm] - mymol->Q_calc[mm][mm]);
                    }
                }
                if (NULL != mymol->gr)
                {
                    ener[ermsESP] += gmx_resp_get_rms(mymol->gr,&wtot);
                    if (NULL != debug)
                        fprintf(debug,"RMS %s = %g\n",
                                mymol->molname,ener[ermsESP]);
                }
            }
            else 
            {
                ener[ermsMU]     = sqr(mymol->dip_calc - mymol->dip_exp); 
            }
            for(j=0; (j<ermsNR); j++)
                etot[j] += ener[j];
        }
    }
    for(j=0; (j<ermsTOT); j++) {
        md->ener[j]       += md->fc[j]*etot[j]/md->nmol_support;
        md->ener[ermsTOT] += md->ener[j];
    }
    sfree(epot);
    if (debug)
    {
        fprintf(debug,"ENER:");
        for(j=0; (j<ermsNR); j++)
            fprintf(debug,"  %8.3f",etot[j]);
        fprintf(debug,"\n");
    }
    /* Global sum energies */
    if (PAR(md->cr)) 
    {
#ifdef GMX_DOUBLE
        gmx_sumd(ermsNR,md->ener,md->cr);
#else
        gmx_sumf(ermsNR,md->ener,md->cr);
#endif
    }
}

static double dipole_function(void *params,double v[])
{
    t_moldip *md = (t_moldip *) params;
    int      i,j,k,zz,nzeta;
    double   chi0,z,J0,bounds=0;
    char     *name,*zeta,*qstr,*rowstr;
    char     zstr[STRLEN],buf[STRLEN];
    
#define HARMONIC(x,xmin,xmax) (x < xmin) ? (sqr(x-xmin)) : ((x > xmax) ? (sqr(x-xmax)) : 0)
  
    /* Set parameters in eem record. There is a penalty if parameters
     * go out of bounds as well.
     */
    k=0;
    while ((name = opt_index_count(md->ic)) != NULL)
    {
        J0 = v[k++];
        bounds += HARMONIC(J0,md->J0_0,md->J0_1);
        if (strcasecmp(name,md->fixchi) != 0) 
        {
            chi0 = v[k++];
            bounds += HARMONIC(chi0,md->Chi0_0,md->Chi0_1);
        }
        else 
            chi0 = gmx_poldata_get_chi0(md->pd,md->iModel,name);
    
        qstr = gmx_poldata_get_qstr(md->pd,md->iModel,name);
        rowstr = gmx_poldata_get_rowstr(md->pd,md->iModel,name);
        nzeta = gmx_poldata_get_nzeta(md->pd,md->iModel,name);
        zstr[0] = '\0';
        for(zz=0; (zz<nzeta); zz++) 
        {
            z = gmx_poldata_get_zeta(md->pd,md->iModel,name,zz);
            if ((0 != z) && (md->bFitZeta))
            {
                z = v[k++];
                bounds += HARMONIC(z,md->w_0,md->w_1);
            }
            sprintf(buf,"  %g",z);
            strcat(zstr,buf);
        }
        gmx_poldata_set_eemprops(md->pd,md->iModel,name,J0,chi0,
                                 zstr,qstr,rowstr);
    }
    if (md->bOptHfac) 
    {
        md->hfac = v[k++];
        if (md->hfac >  md->hfac0) 
            bounds += 100*sqr(md->hfac - md->hfac0);
        else if (md->hfac < -md->hfac0)
            bounds += 100*sqr(md->hfac + md->hfac0);
    }
    for(j=0; (j<ermsNR); j++)
        md->ener[j] = 0;
    calc_moldip_deviation(md);
  
    /* This contribution is not scaled with force constant because
     * it are essential to charge generation convergence and can hence
     * not be left out of the optimization.
     */
    md->ener[ermsBOUNDS] += bounds;
    md->ener[ermsTOT]    += bounds;
    
    return md->ener[ermsTOT];
}

static real guess_new_param(real x,real step,real x0,real x1,gmx_rng_t rng,
                            gmx_bool bRandom)
{
    real r = gmx_rng_uniform_real(rng);
  
    if (bRandom) 
        x = x0+(x1-x0)*r;
    else
        x = x*(1-step+2*step*r);

    if (x < x0)
        return x0;
    else if (x > x1)
        return x1;
    else
        return x;
}

static int guess_all_param(FILE *fplog,t_moldip *md,int run,int iter,real stepsize,
                           gmx_bool bRandom,gmx_rng_t rng,
                           double orig_param[],double test_param[])
{
    double J00,xxx,chi0,zeta;
    char   *name,*qstr,*rowstr;
    char   zstr[STRLEN],buf[STRLEN];
    gmx_bool   bStart = (/*(run == 0) &&*/ (iter == 0));
    gmx_bool   bRand  = bRandom && (iter == 0);
    int    zz,nzeta,k = 0;
    
    fprintf(fplog,"%-5s %10s %10s %10s Run/Iter %d/%d - %s randomization\n","Name",
            "J00","chi0","zeta",run,iter,
            bRand ? "Complete" : (bStart ? "Initial" : "Limited"));
    while ((name = opt_index_count(md->ic)) != NULL) 
    {
        if (bStart)
        {
            J00 = gmx_poldata_get_j00(md->pd,md->iModel,name);
            xxx = guess_new_param(J00,stepsize,md->J0_0,md->J0_1,rng,bRand);
            if (bRand)
                orig_param[k] = xxx;
            else
                orig_param[k] = J00;
            J00 = xxx;
        }
        else 
            J00 = guess_new_param(orig_param[k],stepsize,md->J0_0,md->J0_1,rng,bRand);
        test_param[k++] = J00;
        
        chi0 = gmx_poldata_get_chi0(md->pd,md->iModel,name);
        if (strcasecmp(name,md->fixchi) != 0) 
        {
            if (bStart)
            {
                xxx = guess_new_param(chi0,stepsize,md->Chi0_0,md->Chi0_1,rng,bRand);
                if (bRand)
                    orig_param[k] = xxx;
                else
                    orig_param[k] = chi0;
                chi0 = xxx;
            }
            else 
                chi0 = guess_new_param(orig_param[k],stepsize,md->Chi0_0,md->Chi0_1,rng,bRand);
            test_param[k++] = chi0;
        }
        if ((qstr = gmx_poldata_get_qstr(md->pd,md->iModel,name)) == NULL)
            gmx_fatal(FARGS,"No qstr for atom %s model %d\n",name,md->iModel);
        if ((rowstr = gmx_poldata_get_rowstr(md->pd,md->iModel,name)) == NULL)
            gmx_fatal(FARGS,"No rowstr for atom %s model %d\n",name,md->iModel);
        nzeta   = gmx_poldata_get_nzeta(md->pd,md->iModel,name);
        zstr[0] = '\0';
        for(zz=0; (zz<nzeta); zz++) 
        {
            zeta = gmx_poldata_get_zeta(md->pd,md->iModel,name,zz);
            if ((md->bFitZeta) && (0 != zeta))
            {
                if (bStart)
                {
                    xxx = guess_new_param(zeta,stepsize,md->w_0,md->w_1,rng,bRand);
                    orig_param[k] = (bRand) ? xxx : zeta;
                    zeta = xxx;
                }
                else 
                    zeta = guess_new_param(orig_param[k],stepsize,md->w_0,md->w_1,rng,bRand);
                test_param[k++] = zeta;            
            }
            sprintf(buf,"  %10g",zeta);
            strcat(zstr,buf);
        }
        gmx_poldata_set_eemprops(md->pd,md->iModel,name,J00,chi0,
                                 zstr,qstr,rowstr);
        fprintf(fplog,"%-5s %10g %10g %10s\n",name,J00,chi0,zstr);
    }
    fprintf(fplog,"\n");
    fflush(fplog);
    if (md->bOptHfac) 
        test_param[k++] = md->hfac;
    return k;
}

static void optimize_moldip(FILE *fp,FILE *fplog,const char *convfn,
                            t_moldip *md,int maxiter,real tol,
                            int nrun,int reinit,real stepsize,int seed,
                            gmx_bool bRandom,real stol,output_env_t oenv)
{
    FILE   *cfp=NULL;
    double chi2,chi2_min,wj,rms_nw;
    int    nzeta,zz;
    int    status = 0;
    int    i,k,index,n,nparam;
    double *test_param,*orig_param,*best_param,*start;
    gmx_bool   bMinimum=FALSE;
    double J00,chi0,zeta;
    char   *name,*qstr,*rowstr;
    char   zstr[STRLEN],buf[STRLEN];
    gmx_rng_t rng;
  
    if (MASTER(md->cr)) 
    {
        rng = gmx_rng_init(seed);
  
        nparam = 0;
        while ((name = opt_index_count(md->ic)) != NULL) 
        {
            /* One parameter for J00 and one for chi0 */
            nparam++;
            if (strcasecmp(name,md->fixchi) != 0) 
                nparam++;
            if (md->bFitZeta) 
            {
                nzeta  = gmx_poldata_get_nzeta(md->pd,md->iModel,name);
                for(i=0; (i<nzeta); i++)
                {
                    zeta = gmx_poldata_get_zeta(md->pd,md->iModel,name,i);
                    if (zeta > 0)
                        nparam++;
                }
            }
        }
        /* Check whether we have to optimize the fudge factor for J00 H */
        if (md->hfac != 0)
            nparam++;
        snew(test_param,nparam+1);
        snew(orig_param,nparam+1);
        snew(best_param,nparam+1);
        
        /* Starting point */
        snew(start,nparam);
        
        /* Monitor convergence graphically */
        if (NULL != convfn) 
        {
            cfp = xvgropen(convfn,"Convergence","Value","Iter",oenv);
        }
        chi2_min = GMX_REAL_MAX; 
        for(n=0; (n<nrun); n++) {    
            if ((NULL != fp) && (0 == n)) {
                fprintf(fp,"\nStarting run %d out of %d\n",n+1,nrun);
                fprintf(fp,"%5s %8s %8s %8s %8s %8s %8s %8s\n",
                        "Run","d2","Total","Bounds",
                        "Dipole","Quad.","Charge","ESP");
            }
            if (NULL != cfp)
                fflush(cfp);
                    
            k = guess_all_param(fplog,md,n,0,stepsize,bRandom,rng,
                                orig_param,test_param);
            if (k != nparam)
                gmx_fatal(FARGS,"Inconsistency in guess_all_param: k = %d, should be %d",k,nparam);
            for(k=0; (k<nparam); k++) 
            {
                start[k] = test_param[k];
            }
                
            nmsimplex(cfp,(void *)md,dipole_function,start,nparam,
                      tol,1,maxiter,&chi2);
                
            if (chi2 < chi2_min) {
                bMinimum = TRUE;
                /* Print convergence if needed */
                if (NULL != cfp) 
                    fprintf(cfp,"%5d  ",n*maxiter);
                for(k=0; (k<nparam); k++)
                {
                    best_param[k] = start[k];
                    if (NULL != cfp)
                        fprintf(cfp," %10g",best_param[k]);
                }
                if (NULL != cfp)
                    fprintf(cfp,"\n");
                chi2_min   = chi2;
            }
            
            if (fp) 
                fprintf(fp,"%5d %8.3f %8.3f %8.3f %8.3f %8.3f %8.3f %8.3f\n",
                        n,chi2,
                        sqrt(md->ener[ermsTOT]),
                        sqrt(md->ener[ermsBOUNDS]),
                        sqrt(md->ener[ermsMU]),
                        sqrt(md->ener[ermsQUAD]),
                        sqrt(md->ener[ermsCHARGE]),
                        sqrt(md->ener[ermsESP]));
        }
        
        if (bMinimum) {
            for(k=0; (k<nparam); k++)
                start[k] = best_param[k];
            k = 0;
            while ((name = opt_index_count(md->ic)) != NULL) 
            {
                J00    = start[k++];
                chi0   = gmx_poldata_get_chi0(md->pd,md->iModel,name);
                if (strcasecmp(name,md->fixchi) != 0) 
                    chi0 = start[k++];
                qstr   = gmx_poldata_get_qstr(md->pd,md->iModel,name);
                rowstr = gmx_poldata_get_rowstr(md->pd,md->iModel,name);
                nzeta  = gmx_poldata_get_nzeta(md->pd,md->iModel,name);
                zstr[0] = '\0';
                for(zz=0; (zz<nzeta); zz++)
                {
                    zeta = gmx_poldata_get_zeta(md->pd,md->iModel,name,zz);
                    if ((0 != zeta) && md->bFitZeta)
                        zeta = start[k++];
                    sprintf(buf," %g",zeta);
                    strcat(zstr,buf);
                }
                gmx_poldata_set_eemprops(md->pd,md->iModel,name,J00,chi0,
                                         zstr,qstr,rowstr);
            }
            if (md->bOptHfac) 
                md->hfac = start[k++];
            gmx_assert(k,nparam);
            
            calc_moldip_deviation(md);
            chi2  = sqrt(md->ener[ermsTOT]);
            md->bFinal = TRUE;
            calc_moldip_deviation(md);
            if (fplog)
            {
                fprintf(fplog,"\nMinimum value for RMSD during optimization: %.3f.\n",chi2);
            }
        }
        md->bDone = TRUE;
        calc_moldip_deviation(md);
        
        if (NULL != cfp)
            fclose(cfp);
    }
    else 
    {
        /* Slave calculators */
        do {
            calc_moldip_deviation(md);
        } while (!md->bDone);
    }
}

static real quality_of_fit(real chi2,int N)
{
    return -1;
}

int main(int argc, char *argv[])
{
    static const char *desc[] = {
        "tune_dip read a series of molecules and corresponding experimental",
        "dipole moments from a file, and tunes parameters in an algorithm",
        "until the experimental dipole moments are reproduced by the",
        "charge generating algorithm AX as implemented in the gentop program.[PAR]",
        "Minima and maxima for the parameters can be set, these are however",
        "not strictly enforced, but rather they are penalized with a harmonic",
        "function, for which the force constant can be set explicitly.[PAR]",
        "At every reinit step parameters are changed by a random amount within",
        "the fraction set by step size, and within the boundaries given",
        "by the minima and maxima. If the [TT]-random[tt] flag is",
        "given a completely random set of parameters is generated at the start",
        "of each run. At reinit steps however, the parameters are only changed",
        "slightly, in order to speed-up local search but not global search."
        "In other words, complete random starts are done only at the beginning of each",
        "run, and only when explicitly requested.[PAR]",
        "The absolut dipole moment of a molecule remains unchanged if all the",
        "atoms swap the sign of the charge. To prevent this kind of mirror",
        "effects a penalty is added to the square deviation ",
        "if hydrogen atoms have a negative charge. Similarly a penalty is",
        "added if atoms from row VI or VII in the periodic table have a positive",
        "charge. The penalty is equal to the force constant given on the command line",
        "time the square of the charge.[PAR]",
        "One of the electronegativities (chi) is redundant in the optimization,",
        "only the relative values are meaningful.",
        "Therefore by default we fix the value for hydrogen to what is written",
        "in the eemprops.dat file (or whatever is given with the [tt]-d[TT] flag).",
        "A suitable value would be 2.3, the original, value due to Pauling,",
        "this can by overridden by setting the [tt]-fixchi[TT] flag to something else (e.g. a non-existing atom).[PAR]",
        "A selection of molecules into a training set and a test set (or ignore set)",
        "can be made using option [TT]-sel[tt]. The format of this file is:[BR]",
        "iupac|Train[BR]",
        "iupac|Test[BR]",
        "iupac|Ignore[BR]",
        "and you should ideally have a line for each molecule in the molecule database",
        "([TT]-f[tt] option). Missing molecules will be ignored."
    };
  
    t_filenm fnm[] = {
        { efDAT, "-f", "allmols",    ffREAD  },
        { efDAT, "-d", "gentop",     ffOPTRD },
        { efDAT, "-o", "tunedip",    ffWRITE },
        { efDAT, "-sel", "molselect",ffREAD },
        { efLOG, "-g", "charges",    ffWRITE },
        { efXVG, "-x", "dipcorr",    ffWRITE },
        { efXVG, "-qhisto", "q_histo",    ffWRITE },
        { efXVG, "-qdiff", "q_diff",    ffWRITE },
        { efXVG, "-mudiff", "mu_diff",    ffWRITE },
        { efXVG, "-thetadiff", "theta_diff",    ffWRITE },
        { efXVG, "-espdiff", "esp_diff",    ffWRITE },
        { efXVG, "-conv", "convergence", ffOPTWR }
    };
#define NFILE asize(fnm)
    static int  nrun=1,maxiter=100,reinit=0,seed=1993;
    static int  minimum_data=3,compress=1;
    static real tol=1e-3,stol=1e-6,watoms=1;
    static gmx_bool bRandom=FALSE,bZero=TRUE,bWeighted=TRUE,bOptHfac=FALSE,bQM=FALSE,bCharged=TRUE,bGaussianBug=TRUE,bPol=FALSE,bFitZeta=TRUE;
    static real J0_0=5,Chi0_0=1,w_0=5,step=0.01,hfac=0,rDecrZeta=-1;
    static real J0_1=30,Chi0_1=30,w_1=50,epsr=1;
    static real fc_mu=1,fc_bound=1,fc_quad=1,fc_charge=0,fc_esp=0;
    static real th_toler=170,ph_toler=5,dip_toler=0.5,quad_toler=5,q_toler=0.25;
    static char *opt_elem = NULL,*const_elem=NULL,*fixchi=(char *)"H";
    static char *lot = (char *)"B3LYP/aug-cc-pVTZ";
    static char *qgen[] = { NULL,(char *)"AXp", (char *)"AXs", (char *)"AXg", NULL };
    t_pargs pa[] = {
        { "-tol",   FALSE, etREAL, {&tol},
          "Tolerance for convergence in optimization" },
        { "-maxiter",FALSE, etINT, {&maxiter},
          "Max number of iterations for optimization" },
        { "-reinit", FALSE, etINT, {&reinit},
          "After this many iterations the search vectors are randomized again. A vlue of 0 means this is never done at all." },
        { "-stol",   FALSE, etREAL, {&stol},
          "If reinit is -1 then a reinit will be done as soon as the simplex size is below this treshold." },
        { "-nrun",   FALSE, etINT,  {&nrun},
          "This many runs will be done, before each run a complete randomization will be done" },
        { "-qm",     FALSE, etBOOL, {&bQM},
          "Use only quantum chemistry results (from the levels of theory below) in order to fit the parameters. If not set, experimental values will be used as reference with optional quantum chemistry results, in case no experimental results are available" },
        { "-lot",    FALSE, etSTR,  {&lot},
      "Use this method and level of theory when selecting coordinates and charges. Multiple levels can be specified which will be used in the order given, e.g.  B3LYP/aug-cc-pVTZ:HF/6-311G**" },
        { "-charged",FALSE, etBOOL, {&bCharged},
          "Use charged molecules in the parameter tuning as well" },
        { "-qgen",   FALSE, etENUM, {qgen},
          "Algorithm used for charge generation" },
        { "-fixchi", FALSE, etSTR,  {&fixchi},
          "Electronegativity for this element is fixed. Set to FALSE if you want this variable as well, but read the help text above." },
        { "-j0",    FALSE, etREAL, {&J0_0},
          "Minimum value that J0 (eV) can obtain in fitting" },
        { "-chi0",    FALSE, etREAL, {&Chi0_0},
          "Minimum value that Chi0 (eV) can obtain in fitting" },
        { "-z0",    FALSE, etREAL, {&w_0},
          "Minimum value that inverse radius (1/nm) can obtain in fitting" },
        { "-j1",    FALSE, etREAL, {&J0_1},
          "Maximum value that J0 (eV) can obtain in fitting" },
        { "-chi1",    FALSE, etREAL, {&Chi0_1},
          "Maximum value that Chi0 (eV) can obtain in fitting" },
        { "-z1",    FALSE, etREAL, {&w_1},
          "Maximum value that inverse radius (1/nm) can obtain in fitting" },
        { "-decrzeta", FALSE, etREAL, {&rDecrZeta},
          "Generate decreasing zeta with increasing row numbers for atoms that have multiple distributed charges. In this manner the 1S electrons are closer to the nucleus than 2S electrons and so on. If this number is < 0, nothing is done, otherwise a penalty is imposed in fitting if the Z2-Z1 < this number." },
        { "-fc_bound",    FALSE, etREAL, {&fc_bound},
          "Force constant in the penalty function for going outside the borders given with the above six options." },
        { "-fc_mu",    FALSE, etREAL, {&fc_mu},
          "Force constant in the penalty function for the magnitude of the dipole components." },
        { "-fc_quad",  FALSE, etREAL, {&fc_quad},
          "Force constant in the penalty function for the magnitude of the quadrupole components." },
        { "-fc_esp",   FALSE, etREAL, {&fc_esp},
          "Force constant in the penalty function for the magnitude of the electrostatic potential." },
        { "-fc_charge",  FALSE, etREAL, {&fc_charge},
          "Force constant in the penalty function for the magnitude of the charges with respect to the ESP charges." },
        { "-step",  FALSE, etREAL, {&step},
          "Step size in parameter optimization. Is used as a fraction of the starting value, should be less than 10%. At each reinit step the step size is updated." },
        { "-min_data",  FALSE, etINT, {&minimum_data},
          "Minimum number of data points in order to be able to optimize the parameters for a given atomtype" },
        { "-opt_elem",  FALSE, etSTR, {&opt_elem},
          "Space-separated list of elements to optimize, e.g. \"H C Br\". The other available elements in gentop.dat are left unmodified. If this variable is not set, all elements will be optimized." },
        { "-const_elem",  FALSE, etSTR, {&const_elem},
          "Space-separated list of elements to include but keep constant, e.g. \"O N\". These elements from gentop.dat are left unmodified" },
        { "-seed", FALSE, etINT, {&seed},
          "Random number seed for reinit" },
        { "-random", FALSE, etBOOL, {&bRandom},
          "Generate completely random starting parameters within the limits set by the options. This will be done at the very first step and before each subsequent run." },
        { "-watoms", FALSE, etREAL, {&watoms},
          "Weight for the atoms when fitting the charges to the electrostatic potential. The potential on atoms is usually two orders of magnitude larger than on other points (and negative). For point charges or single smeared charges use zero. For point+smeared charges 1 is recommended (the default)." },
        { "-pol",    FALSE, etBOOL, {&bPol},
          "Add polarizabilities to the topology and coordinate file" },
        { "-fitzeta", FALSE, etBOOL, {&bFitZeta},
          "Controls whether or not the Gaussian/Slater widths are optimized." },
        { "-zero", FALSE, etBOOL, {&bZero},
          "Use molecules with zero dipole in the fit as well" },
        { "-weight", FALSE, etBOOL, {&bWeighted},
          "Perform a weighted fit, by using the errors in the dipoles presented in the input file. This may or may not improve convergence." },
        { "-hfac",  FALSE, etREAL, {&hfac},
          "Fudge factor to scale the J00 of hydrogen by (1 + hfac * qH). Default hfac is 0, means no fudging." },
        { "-epsr", FALSE, etREAL, {&epsr},
          "Relative dielectric constant to account for intramolecular polarization. Should be >= 1." },
        { "-opthfac",  FALSE, etBOOL, {&bOptHfac},
          "[HIDDEN]Optimize the fudge factor to scale the J00 of hydrogen (see above). If set, then [TT]-hfac[tt] set the absolute value of the largest hfac. Above this, a penalty is incurred." },
        { "-dip_toler", FALSE, etREAL, {&dip_toler},
          "Tolerance (Debye) for marking dipole as an outlier in the log file" },
        { "-quad_toler", FALSE, etREAL, {&quad_toler},
          "Tolerance (Buckingham) for marking quadrupole as an outlier in the log file" },
        { "-th_toler", FALSE, etREAL, {&th_toler},
          "Minimum angle to be considered a linear A-B-C bond" },
        { "-ph_toler", FALSE, etREAL, {&ph_toler},
          "Maximum angle to be considered a planar A-B-C/B-C-D torsion" },
        { "-compress", FALSE, etBOOL, {&compress},
          "Compress output XML file" },
        { "-bgaussquad", FALSE, etBOOL, {&bGaussianBug},
          "[HIDDEN]Work around a bug in the off-diagonal quadrupole components in Gaussian" }
    };
    t_moldip  *md;
    FILE      *fp,*out;
    int       iModel;
    t_commrec *cr;
    output_env_t oenv;
    gmx_molselect_t gms;
    time_t    my_t;
    char      pukestr[STRLEN];

    cr = init_par(&argc,&argv);

    if (MASTER(cr))
        CopyRight(stdout,argv[0]);

    parse_common_args(&argc,argv,PCA_CAN_VIEW | (MASTER(cr) ? 0 : PCA_QUIET),
                      NFILE,fnm,asize(pa),pa,asize(desc),desc,0,NULL,&oenv);

    if (qgen[0]) 
        iModel = name2eemtype(qgen[0]);
    else
        iModel = eqgNone;
    
    if (MASTER(cr)) {
        fp = ffopen(opt2fn("-g",NFILE,fnm),"w");

        time(&my_t);
        fprintf(fp,"# This file was created %s",ctime(&my_t));
        fprintf(fp,"# by the following command:\n# %s\n#\n",command_line());
        fprintf(fp,"# %s is part of G R O M A C S:\n#\n",ShortProgram());
        bromacs(pukestr,99);
        fprintf(fp,"# %s\n#\n",pukestr);
    }
    else
        fp = NULL;
    if (MASTER(cr)) 
        gms = gmx_molselect_init(opt2fn("-sel",NFILE,fnm));
    else
        gms = NULL;
    md  = read_moldip(fp ? fp : (debug ? debug : NULL),
                      cr,opt2fn("-f",NFILE,fnm),
                      opt2fn("-d",NFILE,fnm),
                      J0_0,Chi0_0,w_0,J0_1,Chi0_1,w_1,
                      fc_bound,fc_mu,fc_quad,fc_charge,fc_esp,
                      iModel,fixchi,minimum_data,bZero,bWeighted,
                      bOptHfac,hfac,opt_elem,const_elem,
                      bQM,lot,bCharged,oenv,gms,th_toler,ph_toler,dip_toler,
                      bGaussianBug,watoms,rDecrZeta,bPol,bFitZeta,epsr);
    
    optimize_moldip(MASTER(cr) ? stderr : NULL,fp,opt2fn_null("-conv",NFILE,fnm),
                    md,maxiter,tol,nrun,reinit,step,seed,
                    bRandom,stol,oenv);
    if (MASTER(cr)) 
    {
        print_mols(fp,opt2fn("-x",NFILE,fnm),opt2fn("-qhisto",NFILE,fnm),
                   opt2fn("-qdiff",NFILE,fnm),opt2fn("-mudiff",NFILE,fnm),
                   opt2fn("-thetadiff",NFILE,fnm),opt2fn("-espdiff",NFILE,fnm),
                   md->nmol,md->mymol,md->pd,iModel,md->hfac,
                   dip_toler,quad_toler,q_toler,oenv);
  
        ffclose(fp);
  
        gmx_poldata_write(opt2fn("-o",NFILE,fnm),md->pd,md->atomprop,compress);
  
        thanx(stdout);
    }
    
#ifdef GMX_MPI
    gmx_finalize();
#endif

    return 0;
}
