class UsesController < ApplicationController
#  def index
#    @uses = Use.joins(:member).limit(100);
#
#    respond_to do |format|
#      format.html
#    end
#  end

  def show
    @uses = Use.where(member: params[:id]).joins(:member, :source)

    respond_to do |format|
      format.html
    end
  end

end
